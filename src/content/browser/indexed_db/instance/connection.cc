// Copyright 2016 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/indexed_db/instance/connection.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "base/check.h"
#include "base/check_op.h"
#include "base/feature_list.h"
#include "base/functional/callback.h"
#include "base/functional/callback_forward.h"
#include "base/memory/weak_ptr.h"
#include "base/metrics/histogram_functions.h"
#include "base/sequence_checker.h"
#include "base/stl_util.h"
#include "base/strings/string_util.h"
#include "base/time/time.h"
#include "base/trace_event/trace_event.h"
#include "base/types/cxx23_to_underlying.h"
#include "base/unguessable_token.h"
#include "components/services/storage/indexed_db/locks/partitioned_lock_id.h"
#include "components/services/storage/indexed_db/locks/partitioned_lock_manager.h"
#include "components/services/storage/privileged/mojom/indexed_db_client_state_checker.mojom.h"
#include "content/browser/indexed_db/instance/backing_store.h"
#include "content/browser/indexed_db/instance/callback_helpers.h"
#include "content/browser/indexed_db/instance/cursor.h"
#include "content/browser/indexed_db/instance/database_callbacks.h"
#include "content/browser/indexed_db/instance/lock_request_data.h"
#include "content/browser/indexed_db/instance/transaction.h"
#include "content/browser/indexed_db/status.h"
#include "content/public/common/content_features.h"
#include "mojo/public/cpp/bindings/associated_remote.h"
#include "mojo/public/cpp/bindings/callback_helpers.h"
#include "mojo/public/cpp/bindings/message.h"
#include "mojo/public/cpp/bindings/pending_associated_receiver.h"
#include "mojo/public/cpp/bindings/pending_associated_remote.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "mojo/public/cpp/bindings/self_owned_associated_receiver.h"
#include "storage/browser/quota/quota_manager_proxy.h"
#include "third_party/blink/public/mojom/indexeddb/indexeddb.mojom.h"

using blink::IndexedDBIndexKeys;
using blink::IndexedDBKey;
using blink::IndexedDBKeyPath;
using blink::IndexedDBKeyRange;
using std::swap;

namespace blink {
class IndexedDBKeyRange;
}

namespace content::indexed_db {
namespace {

static int32_t g_next_indexed_db_connection_id;

const char kBadTransactionMode[] = "Bad transaction mode";
const char kTransactionAlreadyExists[] = "Transaction already exists";

std::string_view DisallowInactiveClientReasonToString(
    storage::mojom::DisallowInactiveClientReason reason) {
  using enum storage::mojom::DisallowInactiveClientReason;
  switch (reason) {
    case kVersionChangeEvent:
      return "VersionChangeEvent";
    case kTransactionIsAcquiringLocks:
      return "TransactionIsAcquiringLocks";
    case kTransactionIsStartingWhileBlockingOthers:
      return "TransactionIsStartingWhileBlockingOthers";
    case kTransactionIsOngoingAndBlockingOthers:
      return "TransactionIsOngoingAndBlockingOthers";
  }
  NOTREACHED();
}

}  // namespace

// static
mojo::PendingAssociatedRemote<blink::mojom::IDBDatabase>
Connection::MakeSelfOwnedReceiverAndBindRemote(
    std::unique_ptr<Connection> connection) {
  mojo::PendingAssociatedRemote<blink::mojom::IDBDatabase> pending_remote;
  mojo::MakeSelfOwnedAssociatedReceiver(
      std::move(connection),
      pending_remote.InitWithNewEndpointAndPassReceiver());
  return pending_remote;
}

Connection::Connection(BucketContext& bucket_context,
                       base::WeakPtr<Database> database,
                       base::RepeatingClosure on_version_change_ignored,
                       base::OnceCallback<void(Connection*)> on_close,
                       std::unique_ptr<DatabaseCallbacks> callbacks,
                       mojo::Remote<storage::mojom::IndexedDBClientStateChecker>
                           client_state_checker,
                       base::UnguessableToken client_token,
                       int scheduling_priority)
    : id_(g_next_indexed_db_connection_id++),
      bucket_context_handle_(bucket_context),
      database_(std::move(database)),
      on_version_change_ignored_(std::move(on_version_change_ignored)),
      on_close_(std::move(on_close)),
      callbacks_(std::move(callbacks)),
      client_state_checker_(std::move(client_state_checker)),
      client_token_(client_token),
      scheduling_priority_(scheduling_priority) {
  bucket_context_handle_->quota_manager()->NotifyBucketAccessed(
      bucket_context_handle_->bucket_locator(), base::Time::Now());
}

Connection::~Connection() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  is_shutting_down_ = true;
  if (!IsConnected()) {
    return;
  }

  AbortTransactionsAndClose(CloseErrorHandling::kAbortAllReturnLastError,
                            "The connection is destroyed.");
}

bool Connection::IsConnected() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return callbacks_.get();
}

Transaction* Connection::CreateVersionChangeTransaction(
    int64_t id,
    const std::set<int64_t>& scope,
    std::unique_ptr<BackingStore::Transaction> backing_store_transaction) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  CHECK_EQ(GetTransaction(id), nullptr) << "Duplicate transaction id." << id;
  return (transactions_[id] = std::make_unique<Transaction>(
              id, this, scope, blink::mojom::IDBTransactionMode::VersionChange,
              blink::mojom::IDBTransactionDurability::Strict,
              bucket_context_handle_, std::move(backing_store_transaction)))
      .get();
}

void Connection::DisallowInactiveClient(
    storage::mojom::DisallowInactiveClientReason reason,
    base::OnceCallback<void(bool)> callback) {
  if (!client_state_checker_.is_bound()) {
    // If the remote is no longer connected, we expect the client will terminate
    // the connection soon, so marking `was_active` true here.
    std::move(callback).Run(/*was_active=*/true);
    return;
  }

  mojo::Remote<storage::mojom::IndexedDBClientKeepActive>
      client_keep_active_remote;
  client_state_checker_->DisallowInactiveClient(
      id_, reason, client_keep_active_remote.BindNewPipeAndPassReceiver(),
      std::move(callback));
  client_keep_active_remotes_[base::to_underlying(reason)].Add(
      std::move(client_keep_active_remote));

  // TODO(381086791): Remove this histogram when the regression is fixed.
  static constexpr char kClientKeepActiveRemotesCount[] =
      "IndexedDB.ClientKeepActiveRemotesCount";
  size_t remotes_count = 0u;
  for (const auto& remote : client_keep_active_remotes_) {
    base::UmaHistogramCounts1M(
        base::JoinString({kClientKeepActiveRemotesCount,
                          DisallowInactiveClientReasonToString(reason)},
                         "."),
        remote.size());
    remotes_count += remote.size();
  }
  base::UmaHistogramCounts1M(kClientKeepActiveRemotesCount, remotes_count);
}

void Connection::RemoveTransaction(int64_t id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  size_t removed = transactions_.erase(id);
  if (!removed) {
    return;
  }

  base::TimeTicks start = base::TimeTicks::Now();
  bool can_go_inactive = true;

  // If this client is still blocking other clients, leave the keep-actives
  // alive.
  for (const auto& [_, transaction] : transactions_) {
    if (transaction->state() == Transaction::State::STARTED &&
        transaction->IsTransactionBlockingOtherClients(
            /*consider_priority=*/true)) {
      can_go_inactive = false;
      break;
    }
  }
  base::TimeDelta duration = base::TimeTicks::Now() - start;
  if (duration > base::Milliseconds(2)) {
    base::UmaHistogramTimes("IndexedDB.RemoveTransactionLongTimes", duration);
    base::UmaHistogramCounts100000(
        "IndexedDB.RemoveTransactionRequestQueueSize",
        bucket_context_handle_->lock_manager().RequestsWaitingForMetrics());
    base::UmaHistogramCounts100000(
        "IndexedDB.RemoveTransactionConnectionTxnCount", transactions_.size());
  }

  // Safe to make this client inactive.
  if (can_go_inactive) {
    for (auto& remotes : client_keep_active_remotes_) {
      remotes.Clear();
    }
  }
}

void Connection::AbortTransactionAndTearDownOnError(
    Transaction* transaction,
    const DatabaseError& error) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  TRACE_EVENT1("IndexedDB", "Database::Abort(error)", "txn.id",
               transaction->id());
  Status status = transaction->Abort(error);
  if (!status.ok()) {
    bucket_context_handle_->OnDatabaseError(database_.get(), status, {});
  }
}

void Connection::CloseAndReportForceClose(const std::string& message) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!IsConnected()) {
    return;
  }

  AbortTransactionsAndClose(CloseErrorHandling::kAbortAllReturnLastError,
                            message)
      ->OnForcedClose();
}

void Connection::RenameObjectStore(int64_t transaction_id,
                                   int64_t object_store_id,
                                   const std::u16string& new_name) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!IsConnected()) {
    return;
  }

  Transaction* transaction = GetTransaction(transaction_id);
  if (!transaction) {
    return;
  }

  if (transaction->mode() != blink::mojom::IDBTransactionMode::VersionChange) {
    mojo::ReportBadMessage(
        "RenameObjectStore must be called from a version change transaction.");
    return;
  }

  if (!transaction->IsAcceptingRequests()) {
    // TODO(crbug.com/40791538): If the transaction was already committed
    // (or is in the process of being committed) we should kill the renderer.
    // This branch however also includes cases where the browser process aborted
    // the transaction, as currently we don't distinguish that state from the
    // transaction having been committed. So for now simply ignore the request.
    return;
  }

  transaction->ScheduleTask(
      blink::mojom::IDBTaskType::Preemptive,
      base::BindOnce(
          [](int64_t object_store_id, const std::u16string& new_name,
             Transaction* transaction) {
            return transaction->BackingStoreTransaction()->RenameObjectStore(
                object_store_id, new_name);
          },
          object_store_id, new_name));
}

void Connection::CreateTransaction(
    mojo::PendingAssociatedReceiver<blink::mojom::IDBTransaction>
        transaction_receiver,
    int64_t transaction_id,
    const std::vector<int64_t>& object_store_ids,
    blink::mojom::IDBTransactionMode mode,
    blink::mojom::IDBTransactionDurability durability) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!IsConnected()) {
    return;
  }

  if (mode != blink::mojom::IDBTransactionMode::ReadOnly &&
      mode != blink::mojom::IDBTransactionMode::ReadWrite) {
    mojo::ReportBadMessage(kBadTransactionMode);
    return;
  }

  if (GetTransaction(transaction_id)) {
    mojo::ReportBadMessage(kTransactionAlreadyExists);
    return;
  }

  if (durability == blink::mojom::IDBTransactionDurability::Default) {
    switch (GetBucketInfo().durability) {
      case blink::mojom::BucketDurability::kStrict:
        durability = blink::mojom::IDBTransactionDurability::Strict;
        break;
      case blink::mojom::BucketDurability::kRelaxed:
        durability = blink::mojom::IDBTransactionDurability::Relaxed;
        break;
    }
  }

  std::set<int64_t> scope(object_store_ids.begin(), object_store_ids.end());
  Transaction* transaction =
      (transactions_[transaction_id] = std::make_unique<Transaction>(
           transaction_id, this, std::move(scope), mode, durability,
           bucket_context_handle_,
           database_->backing_store_db()->CreateTransaction(durability, mode)))
          .get();

  transaction->BindReceiver(std::move(transaction_receiver));
  database_->RegisterAndScheduleTransaction(transaction);
}

void Connection::VersionChangeIgnored() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!IsConnected()) {
    return;
  }

  on_version_change_ignored_.Run();
}

void Connection::Get(int64_t transaction_id,
                     int64_t object_store_id,
                     int64_t index_id,
                     IndexedDBKeyRange key_range,
                     bool key_only,
                     blink::mojom::IDBDatabase::GetCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!IsConnected()) {
    DatabaseError error(blink::mojom::IDBException::kUnknownError,
                        "Not connected.");
    std::move(callback).Run(blink::mojom::IDBDatabaseGetResult::NewErrorResult(
        blink::mojom::IDBError::New(error.code(), error.message())));
    return;
  }

  Transaction* transaction = GetTransaction(transaction_id);
  if (!transaction) {
    DatabaseError error(blink::mojom::IDBException::kUnknownError,
                        "Unknown transaction.");
    std::move(callback).Run(blink::mojom::IDBDatabaseGetResult::NewErrorResult(
        blink::mojom::IDBError::New(error.code(), error.message())));
    return;
  }

  if (!transaction->IsAcceptingRequests()) {
    // TODO(crbug.com/40791538): If the transaction was already committed
    // (or is in the process of being committed) we should kill the renderer.
    // This branch however also includes cases where the browser process aborted
    // the transaction, as currently we don't distinguish that state from the
    // transaction having been committed. So for now simply ignore the request.
    return;
  }

  blink::mojom::IDBDatabase::GetCallback aborting_callback =
      CreateCallbackAbortOnDestruct<blink::mojom::IDBDatabase::GetCallback,
                                    blink::mojom::IDBDatabaseGetResultPtr>(
          std::move(callback), transaction->AsWeakPtr());

  transaction->ScheduleTask(
      BindWeakOperation(&Database::GetOperation, database_, object_store_id,
                        index_id, std::move(key_range),
                        key_only ? indexed_db::CursorType::kKeyOnly
                                 : indexed_db::CursorType::kKeyAndValue,
                        std::move(aborting_callback)));
}

void Connection::GetAll(int64_t transaction_id,
                        int64_t object_store_id,
                        int64_t index_id,
                        IndexedDBKeyRange key_range,
                        blink::mojom::IDBGetAllResultType result_type,
                        int64_t max_count,
                        blink::mojom::IDBCursorDirection direction,
                        blink::mojom::IDBDatabase::GetAllCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto bind_result_sink = [&callback]() {
    mojo::AssociatedRemote<blink::mojom::IDBDatabaseGetAllResultSink>
        result_sink;
    auto receiver = result_sink.BindNewEndpointAndPassReceiver();
    std::move(callback).Run(std::move(receiver));
    return result_sink;
  };

  if (!IsConnected()) {
    DatabaseError error(blink::mojom::IDBException::kUnknownError,
                        "Not connected.");
    bind_result_sink()->OnError(
        blink::mojom::IDBError::New(error.code(), error.message()));
    return;
  }

  Transaction* transaction = GetTransaction(transaction_id);
  if (!transaction) {
    DatabaseError error(blink::mojom::IDBException::kUnknownError,
                        "Unknown transaction.");
    bind_result_sink()->OnError(
        blink::mojom::IDBError::New(error.code(), error.message()));
    return;
  }

  if (!transaction->IsAcceptingRequests()) {
    // TODO(crbug.com/40791538): If the transaction was already committed
    // (or is in the process of being committed) we should kill the renderer.
    // This branch however also includes cases where the browser process aborted
    // the transaction, as currently we don't distinguish that state from the
    // transaction having been committed. So for now simply ignore the request.
    bind_result_sink();
    return;
  }

  transaction->ScheduleTask(database_->CreateGetAllOperation(
      object_store_id, index_id, std::move(key_range), result_type, max_count,
      direction, std::move(callback), transaction));
}

void Connection::OpenCursor(
    int64_t transaction_id,
    int64_t object_store_id,
    int64_t index_id,
    IndexedDBKeyRange key_range,
    blink::mojom::IDBCursorDirection direction,
    bool key_only,
    blink::mojom::IDBTaskType task_type,
    blink::mojom::IDBDatabase::OpenCursorCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!IsConnected()) {
    DatabaseError error(blink::mojom::IDBException::kUnknownError,
                        "Not connected.");
    std::move(callback).Run(
        blink::mojom::IDBDatabaseOpenCursorResult::NewErrorResult(
            blink::mojom::IDBError::New(error.code(), error.message())));
    return;
  }

  Transaction* transaction = GetTransaction(transaction_id);
  if (!transaction) {
    DatabaseError error(blink::mojom::IDBException::kUnknownError,
                        "Unknown transaction.");
    std::move(callback).Run(
        blink::mojom::IDBDatabaseOpenCursorResult::NewErrorResult(
            blink::mojom::IDBError::New(error.code(), error.message())));
    return;
  }

  if (!transaction->IsAcceptingRequests()) {
    // TODO(crbug.com/40791538): If the transaction was already committed
    // (or is in the process of being committed) we should kill the renderer.
    // This branch however also includes cases where the browser process aborted
    // the transaction, as currently we don't distinguish that state from the
    // transaction having been committed. So for now simply ignore the request.
    return;
  }

  blink::mojom::IDBDatabase::OpenCursorCallback aborting_callback =
      CreateCallbackAbortOnDestruct<
          blink::mojom::IDBDatabase::OpenCursorCallback,
          blink::mojom::IDBDatabaseOpenCursorResultPtr>(
          std::move(callback), transaction->AsWeakPtr());

  if (transaction->mode() != blink::mojom::IDBTransactionMode::VersionChange &&
      task_type == blink::mojom::IDBTaskType::Preemptive) {
    mojo::ReportBadMessage(
        "OpenCursor with |Preemptive| task type must be called from a version "
        "change transaction.");
    return;
  }

  std::unique_ptr<Database::OpenCursorOperationParams> params(
      std::make_unique<Database::OpenCursorOperationParams>());
  params->object_store_id = object_store_id;
  params->index_id = index_id;
  params->key_range = std::move(key_range);
  params->direction = direction;
  params->cursor_type = key_only ? indexed_db::CursorType::kKeyOnly
                                 : indexed_db::CursorType::kKeyAndValue;
  params->task_type = task_type;
  params->callback = std::move(aborting_callback);
  transaction->ScheduleTask(BindWeakOperation(&Database::OpenCursorOperation,
                                              database_, std::move(params),
                                              GetBucketLocator()));
}

void Connection::Count(int64_t transaction_id,
                       int64_t object_store_id,
                       int64_t index_id,
                       IndexedDBKeyRange key_range,
                       CountCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto wrapped_callback = mojo::WrapCallbackWithDefaultInvokeIfNotRun(
      std::move(callback), /*success=*/false, 0);

  if (!IsConnected()) {
    return;
  }

  Transaction* transaction = GetTransaction(transaction_id);
  if (!transaction || !transaction->IsAcceptingRequests()) {
    // TODO(crbug.com/40791538): If the transaction was already committed
    // (or is in the process of being committed) we should kill the renderer.
    // This branch however also includes cases where the browser process aborted
    // the transaction, as currently we don't distinguish that state from the
    // transaction having been committed. So for now simply ignore the request.
    return;
  }

  transaction->ScheduleTask(BindWeakOperation(
      &Database::CountOperation, database_, object_store_id, index_id,
      std::move(key_range), std::move(wrapped_callback)));
}

void Connection::DeleteRange(int64_t transaction_id,
                             int64_t object_store_id,
                             IndexedDBKeyRange key_range,
                             DeleteRangeCallback success_callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto wrapped_callback = mojo::WrapCallbackWithDefaultInvokeIfNotRun(
      std::move(success_callback), /*success=*/false);

  if (!IsConnected()) {
    return;
  }

  Transaction* transaction = GetTransaction(transaction_id);
  if (!transaction) {
    return;
  }

  if (!transaction->IsAcceptingRequests()) {
    // TODO(crbug.com/40791538): If the transaction was already committed
    // (or is in the process of being committed) we should kill the renderer.
    // This branch however also includes cases where the browser process aborted
    // the transaction, as currently we don't distinguish that state from the
    // transaction having been committed. So for now simply ignore the request.
    return;
  }

  transaction->ScheduleTask(BindWeakOperation(
      &Database::DeleteRangeOperation, database_, object_store_id,
      std::move(key_range), std::move(wrapped_callback)));
}

void Connection::GetKeyGeneratorCurrentNumber(
    int64_t transaction_id,
    int64_t object_store_id,
    GetKeyGeneratorCurrentNumberCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto wrapped_callback = mojo::WrapCallbackWithDefaultInvokeIfNotRun(
      std::move(callback), -1,
      blink::mojom::IDBError::New(
          blink::mojom::IDBException::kIgnorableAbortError,
          u"Aborting due to unknown failure."));

  Transaction* transaction = GetTransaction(transaction_id);
  if (!transaction) {
    return;
  }

  if (!transaction->IsAcceptingRequests()) {
    // TODO(crbug.com/40791538): If the transaction was already committed
    // (or is in the process of being committed) we should kill the renderer.
    // This branch however also includes cases where the browser process aborted
    // the transaction, as currently we don't distinguish that state from the
    // transaction having been committed. So for now simply ignore the request.
    return;
  }

  transaction->ScheduleTask(BindWeakOperation(
      &Database::GetKeyGeneratorCurrentNumberOperation, database_,
      object_store_id, std::move(wrapped_callback)));
}

void Connection::Clear(int64_t transaction_id,
                       int64_t object_store_id,
                       ClearCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto wrapped_callback = mojo::WrapCallbackWithDefaultInvokeIfNotRun(
      std::move(callback), /*success=*/false);

  if (!IsConnected()) {
    return;
  }

  Transaction* transaction = GetTransaction(transaction_id);
  if (!transaction || !transaction->IsAcceptingRequests()) {
    // TODO(crbug.com/40791538): If the transaction was already committed
    // (or is in the process of being committed) we should kill the renderer.
    // This branch however also includes cases where the browser process aborted
    // the transaction, as currently we don't distinguish that state from the
    // transaction having been committed. So for now simply ignore the request.
    return;
  }

  transaction->ScheduleTask(BindWeakOperation(&Database::ClearOperation,
                                              database_, object_store_id,
                                              std::move(wrapped_callback)));
}

void Connection::CreateIndex(int64_t transaction_id,
                             int64_t object_store_id,
                             const blink::IndexedDBIndexMetadata& index) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!IsConnected()) {
    return;
  }

  Transaction* transaction = GetTransaction(transaction_id);
  if (!transaction) {
    return;
  }

  if (transaction->mode() != blink::mojom::IDBTransactionMode::VersionChange) {
    mojo::ReportBadMessage(
        "CreateIndex must be called from a version change transaction.");
    return;
  }

  if (!transaction->IsAcceptingRequests()) {
    // TODO(crbug.com/40791538): If the transaction was already committed
    // (or is in the process of being committed) we should kill the renderer.
    // This branch however also includes cases where the browser process aborted
    // the transaction, as currently we don't distinguish that state from the
    // transaction having been committed. So for now simply ignore the request.
    return;
  }

  transaction->ScheduleTask(
      blink::mojom::IDBTaskType::Preemptive,
      base::BindOnce(
          [](int64_t object_store_id, blink::IndexedDBIndexMetadata index,
             Transaction* transaction) {
            return transaction->BackingStoreTransaction()->CreateIndex(
                object_store_id, std::move(index));
          },
          object_store_id, index));
}

void Connection::DeleteIndex(int64_t transaction_id,
                             int64_t object_store_id,
                             int64_t index_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!IsConnected()) {
    return;
  }

  Transaction* transaction = GetTransaction(transaction_id);
  if (!transaction) {
    return;
  }

  if (transaction->mode() != blink::mojom::IDBTransactionMode::VersionChange) {
    mojo::ReportBadMessage(
        "DeleteIndex must be called from a version change transaction.");
    return;
  }

  if (!transaction->IsAcceptingRequests()) {
    // TODO(crbug.com/40791538): If the transaction was already committed
    // (or is in the process of being committed) we should kill the renderer.
    // This branch however also includes cases where the browser process aborted
    // the transaction, as currently we don't distinguish that state from the
    // transaction having been committed. So for now simply ignore the request.
    return;
  }
  transaction->ScheduleTask(base::BindOnce(
      [](int64_t object_store_id, int64_t index_id, Transaction* transaction) {
        return transaction->BackingStoreTransaction()->DeleteIndex(
            object_store_id, index_id);
      },
      object_store_id, index_id));
}

void Connection::RenameIndex(int64_t transaction_id,
                             int64_t object_store_id,
                             int64_t index_id,
                             const std::u16string& new_name) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!IsConnected()) {
    return;
  }

  Transaction* transaction = GetTransaction(transaction_id);
  if (!transaction) {
    return;
  }

  if (transaction->mode() != blink::mojom::IDBTransactionMode::VersionChange) {
    mojo::ReportBadMessage(
        "RenameIndex must be called from a version change transaction.");
    return;
  }

  if (!transaction->IsAcceptingRequests()) {
    // TODO(crbug.com/40791538): If the transaction was already committed
    // (or is in the process of being committed) we should kill the renderer.
    // This branch however also includes cases where the browser process aborted
    // the transaction, as currently we don't distinguish that state from the
    // transaction having been committed. So for now simply ignore the request.
    return;
  }

  transaction->ScheduleTask(base::BindOnce(
      [](int64_t object_store_id, int64_t index_id, std::u16string new_name,
         Transaction* transaction) {
        return transaction->BackingStoreTransaction()->RenameIndex(
            object_store_id, index_id, new_name);
      },
      object_store_id, index_id, new_name));
}

void Connection::Abort(int64_t transaction_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!IsConnected()) {
    return;
  }

  Transaction* transaction = GetTransaction(transaction_id);
  if (!transaction) {
    return;
  }

  AbortTransactionAndTearDownOnError(
      transaction, DatabaseError(blink::mojom::IDBException::kAbortError,
                                 "Transaction aborted by user."));
}

void Connection::DidBecomeInactive() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!IsConnected()) {
    return;
  }

  for (const auto& [_, transaction] : transactions_) {
    // If the transaction is holding the locks while others are waiting for
    // the acquisition, we should disallow the activation for this client
    // so the lock is immediately available.
    transaction->DontAllowInactiveClientToBlockOthers(
        storage::mojom::DisallowInactiveClientReason::
            kTransactionIsOngoingAndBlockingOthers);
  }
}

void Connection::UpdatePriority(int new_priority) {
  scheduling_priority_ = new_priority;

  for (const auto& [_, transaction] : transactions_) {
    transaction->OnSchedulingPriorityUpdated(new_priority);
  }

  // Null after `AbortTransactionsAndClose()`.
  if (bucket_context()) {
    bucket_context()->OnConnectionPriorityUpdated();
  }

  // TODO(crbug.com/359623664): consider reordering transactions already in the
  // queue. For now the priority change will only impact where new transactions
  // are placed (whether they skip past the existing ones).
}

const storage::BucketInfo& Connection::GetBucketInfo() {
  CHECK(bucket_context());
  return bucket_context()->bucket_info();
}

storage::BucketLocator Connection::GetBucketLocator() {
  CHECK(bucket_context());
  return bucket_context()->bucket_locator();
}

Transaction* Connection::GetTransaction(int64_t id) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto it = transactions_.find(id);
  if (it == transactions_.end()) {
    return nullptr;
  }
  return it->second.get();
}

std::unique_ptr<DatabaseCallbacks> Connection::AbortTransactionsAndClose(
    CloseErrorHandling error_handling,
    const std::string& message) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!IsConnected()) {
    return {};
  }

  DCHECK(database_);

  // Finish up any transaction, in case there were any running.
  DatabaseError error(blink::mojom::IDBException::kUnknownError,
                      "Connection is closing because of: " + message);
  Status status;
  switch (error_handling) {
    case CloseErrorHandling::kReturnOnFirstError:
      status = AbortAllTransactions(error);
      break;
    case CloseErrorHandling::kAbortAllReturnLastError:
      status = AbortAllTransactionsAndIgnoreErrors(error);
      break;
  }

  std::unique_ptr<DatabaseCallbacks> callbacks = std::move(callbacks_);
  std::move(on_close_).Run(this);
  for (auto& remotes : client_keep_active_remotes_) {
    remotes.Clear();
  }
  bucket_context_handle_->quota_manager()->NotifyBucketAccessed(
      bucket_context_handle_->bucket_locator(), base::Time::Now());
  if (!status.ok()) {
    bucket_context_handle_->OnDatabaseError(database_.get(), status, {});
  }
  bucket_context_handle_.Release();
  return callbacks;
}

Status Connection::AbortAllTransactionsAndIgnoreErrors(
    const DatabaseError& error) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  Status last_error;
  for (const auto& pair : transactions_) {
    auto& transaction = pair.second;
    if (transaction->state() != Transaction::FINISHED) {
      TRACE_EVENT1("IndexedDB", "Database::Abort(error)", "transaction.id",
                   transaction->id());
      Status status = transaction->Abort(error);
      if (!status.ok()) {
        last_error = status;
      }
    }
  }
  return last_error;
}

Status Connection::AbortAllTransactions(const DatabaseError& error) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  for (auto& [_, transaction] : transactions_) {
    if (transaction->state() != Transaction::FINISHED) {
      TRACE_EVENT1("IndexedDB", "Database::Abort(error)", "transaction.id",
                   transaction->id());
      IDB_RETURN_IF_ERROR(transaction->Abort(error));
    }
  }
  return Status::OK();
}

// static
bool Connection::HasHigherPriorityThan(const PartitionedLockHolder* this_one,
                                       const PartitionedLockHolder& other) {
  if (!base::FeatureList::IsEnabled(
          features::kIdbPrioritizeForegroundClients)) {
    return false;
  }

  auto* this_lock_request_data = static_cast<LockRequestData*>(
      this_one->GetUserData(LockRequestData::kKey));
  if (!this_lock_request_data) {
    return false;
  }

  auto* other_lock_request_data =
      static_cast<LockRequestData*>(other.GetUserData(LockRequestData::kKey));
  if (!other_lock_request_data) {
    return false;
  }

  if (this_lock_request_data->client_token ==
      other_lock_request_data->client_token) {
    return false;
  }

  return this_lock_request_data->scheduling_priority <
         other_lock_request_data->scheduling_priority;
}

bool Connection::IsHoldingLocks(
    const std::vector<PartitionedLockId>& lock_ids) const {
  return std::ranges::any_of(
      transactions_,
      [&](const std::pair<const int64_t, std::unique_ptr<Transaction>>&
              existing_transaction) {
        return !base::STLSetIntersection<std::vector<PartitionedLockId>>(
                    lock_ids, existing_transaction.second->lock_ids())
                    .empty();
      });
}

}  // namespace content::indexed_db
