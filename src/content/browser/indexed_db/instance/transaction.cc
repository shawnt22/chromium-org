// Copyright 2013 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/indexed_db/instance/transaction.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "base/check.h"
#include "base/check_op.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/location.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/metrics/histogram_functions.h"
#include "base/notreached.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "base/time/time.h"
#include "base/trace_event/trace_event.h"
#include "base/types/expected_macros.h"
#include "base/unguessable_token.h"
#include "components/services/storage/indexed_db/locks/partitioned_lock_manager.h"
#include "components/services/storage/privileged/mojom/indexed_db_client_state_checker.mojom-shared.h"
#include "components/services/storage/privileged/mojom/indexed_db_internals_types.mojom-forward.h"
#include "components/services/storage/privileged/mojom/indexed_db_internals_types.mojom-shared.h"
#include "components/services/storage/privileged/mojom/indexed_db_internals_types.mojom.h"
#include "components/services/storage/public/mojom/blob_storage_context.mojom-shared.h"
#include "content/browser/indexed_db/indexed_db_external_object.h"
#include "content/browser/indexed_db/indexed_db_external_object_storage.h"
#include "content/browser/indexed_db/indexed_db_leveldb_coding.h"
#include "content/browser/indexed_db/instance/bucket_context.h"
#include "content/browser/indexed_db/instance/bucket_context_handle.h"
#include "content/browser/indexed_db/instance/callback_helpers.h"
#include "content/browser/indexed_db/instance/connection.h"
#include "content/browser/indexed_db/instance/cursor.h"
#include "content/browser/indexed_db/instance/database.h"
#include "content/browser/indexed_db/instance/database_callbacks.h"
#include "content/browser/indexed_db/instance/index_writer.h"
#include "content/browser/indexed_db/instance/lock_request_data.h"
#include "content/browser/indexed_db/status.h"
#include "mojo/public/cpp/bindings/message.h"
#include "mojo/public/cpp/bindings/pending_associated_receiver.h"
#include "third_party/blink/public/mojom/indexeddb/indexeddb.mojom.h"

using blink::IndexedDBIndexKeys;
using blink::IndexedDBKey;
using blink::IndexedDBKeyPath;
using blink::IndexedDBObjectStoreMetadata;

namespace content::indexed_db {

namespace {

std::string WriteBlobToFileResultToString(
    storage::mojom::WriteBlobToFileResult result) {
  switch (result) {
    case storage::mojom::WriteBlobToFileResult::kError:
      return "Error";
    case storage::mojom::WriteBlobToFileResult::kBadPath:
      return "BadPath";
    case storage::mojom::WriteBlobToFileResult::kInvalidBlob:
      return "InvalidBlob";
    case storage::mojom::WriteBlobToFileResult::kIOError:
      return "IOError";
    case storage::mojom::WriteBlobToFileResult::kTimestampError:
      return "TimestampError";
    case storage::mojom::WriteBlobToFileResult::kSuccess:
      return "Success";
  }
  NOTREACHED();
}

// Disabled in some tests.
bool g_inactivity_timeout_enabled = true;

// Used for UMA metrics - do not change values.
enum UmaIDBException {
  UmaIDBExceptionUnknownError = 0,
  UmaIDBExceptionConstraintError = 1,
  UmaIDBExceptionDataError = 2,
  UmaIDBExceptionVersionError = 3,
  UmaIDBExceptionAbortError = 4,
  UmaIDBExceptionQuotaError = 5,
  UmaIDBExceptionTimeoutError = 6,
  UmaIDBExceptionExclusiveMaxValue = 7
};

// Used for UMA metrics - do not change mappings.
UmaIDBException ExceptionCodeToUmaEnum(blink::mojom::IDBException code) {
  switch (code) {
    case blink::mojom::IDBException::kUnknownError:
      return UmaIDBExceptionUnknownError;
    case blink::mojom::IDBException::kConstraintError:
      return UmaIDBExceptionConstraintError;
    case blink::mojom::IDBException::kDataError:
      return UmaIDBExceptionDataError;
    case blink::mojom::IDBException::kVersionError:
      return UmaIDBExceptionVersionError;
    case blink::mojom::IDBException::kAbortError:
      return UmaIDBExceptionAbortError;
    case blink::mojom::IDBException::kQuotaError:
      return UmaIDBExceptionQuotaError;
    case blink::mojom::IDBException::kTimeoutError:
      return UmaIDBExceptionTimeoutError;
    default:
      NOTREACHED();
  }
}

}  // namespace

Transaction::TaskQueue::TaskQueue() = default;
Transaction::TaskQueue::~TaskQueue() = default;

void Transaction::TaskQueue::clear() {
  while (!queue_.empty()) {
    queue_.pop();
  }
}

Transaction::Operation Transaction::TaskQueue::pop() {
  DCHECK(!queue_.empty());
  Operation task = std::move(queue_.front());
  queue_.pop();
  return task;
}

Transaction::Transaction(
    int64_t id,
    Connection* connection,
    const std::set<int64_t>& object_store_ids,
    blink::mojom::IDBTransactionMode mode,
    blink::mojom::IDBTransactionDurability durability,
    BucketContextHandle bucket_context,
    std::unique_ptr<BackingStore::Transaction> backing_store_transaction)
    : id_(id),
      object_store_ids_(object_store_ids),
      mode_(mode),
      durability_(durability),
      connection_(connection->GetWeakPtr()),
      bucket_context_(std::move(bucket_context)),
      backing_store_transaction_(std::move(backing_store_transaction)),
      receiver_(this) {
  TRACE_EVENT_NESTABLE_ASYNC_BEGIN0("IndexedDB", "Transaction::lifetime", this);

  locks_receiver_.SetUserData(
      LockRequestData::kKey,
      std::make_unique<LockRequestData>(connection->client_token(),
                                        connection->scheduling_priority()));

  database_ = connection_->database();
  if (database_) {
    if (mode_ == blink::mojom::IDBTransactionMode::VersionChange) {
      lock_ids_.insert(GetDatabaseLockId(database_->name()));
    } else {
      for (const PartitionedLockManager::PartitionedLockRequest& lock_request :
           BuildLockRequests()) {
        lock_ids_.insert(lock_request.lock_id);
      }
    }
  }

  diagnostics_.tasks_scheduled = 0;
  diagnostics_.tasks_completed = 0;
  diagnostics_.creation_time = base::Time::Now();
  SetState(state_);  // Process the initial state.
}

Transaction::~Transaction() {
  TRACE_EVENT_NESTABLE_ASYNC_END0("IndexedDB", "Transaction::lifetime", this);
  // It shouldn't be possible for this object to get deleted until it's either
  // complete or aborted.
  DCHECK_EQ(state_, FINISHED);
  DCHECK(preemptive_task_queue_.empty());
  DCHECK_EQ(pending_preemptive_events_, 0);
  DCHECK(task_queue_.empty());
  DCHECK(!processing_event_queue_);
}

void Transaction::BindReceiver(
    mojo::PendingAssociatedReceiver<blink::mojom::IDBTransaction>
        mojo_receiver) {
  receiver_.Bind(std::move(mojo_receiver));
}

void Transaction::SetCommitFlag() {
  // The frontend suggests that we commit, but we may have previously initiated
  // an abort.
  if (!IsAcceptingRequests()) {
    return;
  }

  is_commit_pending_ = true;
  bucket_context_->QueueRunTasks();
}

void Transaction::ScheduleTask(blink::mojom::IDBTaskType type, Operation task) {
  if (state_ == FINISHED) {
    return;
  }

  ResetTimeoutTimer();
  used_ = true;
  if (type == blink::mojom::IDBTaskType::Normal) {
    task_queue_.push(std::move(task));
    ++diagnostics_.tasks_scheduled;
    NotifyOfIdbInternalsRelevantChange();
  } else {
    preemptive_task_queue_.push(std::move(task));
  }
  if (state() == STARTED) {
    bucket_context_->QueueRunTasks();
  }
}

Status Transaction::Abort(const DatabaseError& error) {
  if (state_ == FINISHED) {
    return Status::OK();
  }

  base::UmaHistogramEnumeration("WebCore.IndexedDB.TransactionAbortReason",
                                ExceptionCodeToUmaEnum(error.code()),
                                UmaIDBExceptionExclusiveMaxValue);

  aborted_ = true;
  ResetTimeoutTimer();

  SetState(FINISHED);

  if (backing_store_transaction_begun_) {
    backing_store_transaction_->Rollback();
  }

  preemptive_task_queue_.clear();
  pending_preemptive_events_ = 0;

  task_queue_.clear();

  // Backing store resources (held via cursors) must be released
  // before script callbacks are fired, as the script callbacks may
  // release references and allow the backing store itself to be
  // released, and order is critical.
  CloseOpenCursors();
  backing_store_transaction_.reset();

  // Transactions must also be marked as completed before the
  // front-end is notified, as the transaction completion unblocks
  // operations like closing connections.
  locks_receiver_.locks.clear();
  locks_receiver_.CancelLockRequest();

  connection()->callbacks()->OnAbort(*this, error);

  bucket_context_->QueueRunTasks();
  bucket_context_.Release();
  return Status::OK();
}

// static
Status Transaction::CommitPhaseTwoProxy(Transaction* transaction) {
  return transaction->CommitPhaseTwo();
}

bool Transaction::IsTaskQueueEmpty() const {
  return preemptive_task_queue_.empty() && task_queue_.empty();
}

bool Transaction::HasPendingTasks() const {
  return pending_preemptive_events_ || !IsTaskQueueEmpty();
}

void Transaction::RegisterOpenCursor(Cursor* cursor) {
  open_cursors_.insert(cursor);
}

void Transaction::UnregisterOpenCursor(Cursor* cursor) {
  open_cursors_.erase(cursor);
}

void Transaction::DontAllowInactiveClientToBlockOthers(
    storage::mojom::DisallowInactiveClientReason reason) {
  if (state_ == STARTED && IsTransactionBlockingOtherClients()) {
    connection_->DisallowInactiveClient(reason, base::DoNothing());
  }
}

bool Transaction::IsTransactionBlockingOtherClients(
    bool consider_priority) const {
  CHECK_EQ(state_, STARTED);

  if (database_->OnlyHasOneClient()) {
    return false;
  }

  base::TimeTicks start = base::TimeTicks::Now();
  std::optional<int> scheduling_priority;
  if (consider_priority) {
    scheduling_priority = connection_->scheduling_priority();
  }
  const bool is_blocking_others =
      bucket_context_->lock_manager().IsBlockingAnyRequest(
          lock_ids(),
          base::BindRepeating(
              [](std::optional<int> this_priority,
                 const base::UnguessableToken& this_token,
                 PartitionedLockHolder* blocked_lock_holder) {
                auto* lock_request_data = static_cast<LockRequestData*>(
                    blocked_lock_holder->GetUserData(LockRequestData::kKey));
                if (!lock_request_data) {
                  return true;
                }
                // If `this`
                //   * comes from a background client (priority > 0), and
                //   * is equal or higher priority than the blocked
                //   transaction's client
                //     (aka equally or less severely throttled)
                // then don't worry about blocking it.
                if (this_priority && (*this_priority > 0) &&
                    (*this_priority <=
                     lock_request_data->scheduling_priority)) {
                  return false;
                }
                return lock_request_data->client_token != this_token;
              },
              scheduling_priority, connection_->client_token()));
  base::TimeDelta duration = base::TimeTicks::Now() - start;
  if (duration > base::Milliseconds(2)) {
    base::UmaHistogramTimes("IndexedDB.CalculateBlockingStatusLongTimes",
                            duration);
    base::UmaHistogramCounts100000(
        "IndexedDB.CalculateBlockingStatusRequestQueueSize",
        bucket_context_->lock_manager().RequestsWaitingForMetrics());
  }
  return is_blocking_others;
}

void Transaction::Start() {
  // The transaction has the potential to be aborted after the Start() task was
  // posted.
  if (state_ == FINISHED) {
    DCHECK(locks_receiver_.locks.empty());
    return;
  }
  DCHECK_EQ(CREATED, state_);
  std::optional scheduling_priority_at_last_state_change =
      scheduling_priority_at_last_state_change_;
  SetState(STARTED);
  DCHECK(!locks_receiver_.locks.empty());
  diagnostics_.start_time = base::Time::Now();

  // If the client is in BFCache, the transaction will get stuck, so evict it if
  // necessary.
  DontAllowInactiveClientToBlockOthers(
      storage::mojom::DisallowInactiveClientReason::
          kTransactionIsStartingWhileBlockingOthers);

  const base::TimeDelta time_queued =
      diagnostics_.start_time - diagnostics_.creation_time;
  switch (mode_) {
    case blink::mojom::IDBTransactionMode::ReadOnly:
      base::UmaHistogramMediumTimes(
          "WebCore.IndexedDB.Transaction.ReadOnly.TimeQueued", time_queued);
      if (scheduling_priority_at_last_state_change == 0) {
        base::UmaHistogramMediumTimes(
            "WebCore.IndexedDB.Transaction.ReadOnly.TimeQueued.Foreground",
            time_queued);
      }
      break;
    case blink::mojom::IDBTransactionMode::ReadWrite:
      base::UmaHistogramMediumTimes(
          "WebCore.IndexedDB.Transaction.ReadWrite.TimeQueued", time_queued);
      if (scheduling_priority_at_last_state_change == 0) {
        base::UmaHistogramMediumTimes(
            "WebCore.IndexedDB.Transaction.ReadWrite.TimeQueued.Foreground",
            time_queued);
      }
      break;
    case blink::mojom::IDBTransactionMode::VersionChange:
      base::UmaHistogramMediumTimes(
          "WebCore.IndexedDB.Transaction.VersionChange.TimeQueued",
          time_queued);
      if (scheduling_priority_at_last_state_change == 0) {
        base::UmaHistogramMediumTimes(
            "WebCore.IndexedDB.Transaction.VersionChange.TimeQueued.Foreground",
            time_queued);
      }
      break;
  }

  bucket_context_->QueueRunTasks();
}

// static
void Transaction::DisableInactivityTimeoutForTesting() {
  g_inactivity_timeout_enabled = false;
}

void Transaction::CreateObjectStore(int64_t object_store_id,
                                    const std::u16string& name,
                                    const IndexedDBKeyPath& key_path,
                                    bool auto_increment) {
  if (mode() != blink::mojom::IDBTransactionMode::VersionChange) {
    mojo::ReportBadMessage(
        "CreateObjectStore must be called from a version change transaction.");
    return;
  }

  if (!IsAcceptingRequests() || !connection()->IsConnected()) {
    return;
  }

  ScheduleTask(
      blink::mojom::IDBTaskType::Preemptive,
      base::BindOnce(
          [](int64_t object_store_id, const std::u16string& name,
             const IndexedDBKeyPath& key_path, bool auto_increment,
             Transaction* transaction) {
            return transaction->BackingStoreTransaction()->CreateObjectStore(
                object_store_id, name, key_path, auto_increment);
          },
          object_store_id, name, key_path, auto_increment));
}

void Transaction::DeleteObjectStore(int64_t object_store_id) {
  if (mode() != blink::mojom::IDBTransactionMode::VersionChange) {
    mojo::ReportBadMessage(
        "DeleteObjectStore must be called from a version change transaction.");
    return;
  }

  if (!IsAcceptingRequests() || !connection()->IsConnected()) {
    return;
  }

  ScheduleTask(base::BindOnce(
      [](int64_t object_store_id, Transaction* transaction) {
        return transaction->BackingStoreTransaction()->DeleteObjectStore(
            object_store_id);
      },
      object_store_id));
}

void Transaction::Put(int64_t object_store_id,
                      blink::mojom::IDBValuePtr input_value,
                      IndexedDBKey key,
                      blink::mojom::IDBPutMode mode,
                      std::vector<IndexedDBIndexKeys> index_keys,
                      blink::mojom::IDBTransaction::PutCallback callback) {
  if (!IsAcceptingRequests()) {
    return;
  }

  if (!connection()->IsConnected()) {
    DatabaseError error(blink::mojom::IDBException::kUnknownError,
                        "Not connected.");
    std::move(callback).Run(
        blink::mojom::IDBTransactionPutResult::NewErrorResult(
            blink::mojom::IDBError::New(error.code(), error.message())));
    return;
  }

  std::vector<IndexedDBExternalObject> external_objects;
  uint64_t total_blob_size = 0;
  if (!input_value->external_objects.empty()) {
    total_blob_size = CreateExternalObjects(input_value, &external_objects);
  }

  // Increment the total transaction size by the size of this put.
  preliminary_size_estimate_ +=
      input_value->bits.size() + key.size_estimate() + total_blob_size;
  // Warm up the disk space cache.
  bucket_context()->CheckCanUseDiskSpace(preliminary_size_estimate_, {});

  IndexedDBValue value;
  value.bits = std::move(input_value->bits);
  value.external_objects = std::move(external_objects);

  blink::mojom::IDBTransaction::PutCallback wrapped_callback =
      CreateCallbackAbortOnDestruct<blink::mojom::IDBTransaction::PutCallback,
                                    blink::mojom::IDBTransactionPutResultPtr>(
          std::move(callback), AsWeakPtr());

  // This is decremented in DoPut.
  in_flight_memory_ += value.SizeEstimate();
  ScheduleTask(base::BindOnce(&Transaction::DoPut, base::Unretained(this),
                              object_store_id, std::move(value), std::move(key),
                              mode, std::move(index_keys),
                              std::move(wrapped_callback)));
}

Status Transaction::DoPut(int64_t object_store_id,
                          IndexedDBValue value,
                          IndexedDBKey key,
                          blink::mojom::IDBPutMode put_mode,
                          std::vector<IndexedDBIndexKeys> index_keys,
                          blink::mojom::IDBTransaction::PutCallback callback,
                          Transaction* txn) {
  DCHECK_EQ(this, txn);
  TRACE_EVENT2("IndexedDB", "Database::PutOperation", "txn.id", id(), "size",
               value.SizeEstimate());
  DCHECK_NE(mode(), blink::mojom::IDBTransactionMode::ReadOnly);
  bool key_was_generated = false;
  in_flight_memory_ -= value.SizeEstimate();
  DCHECK(in_flight_memory_.IsValid());

  auto on_put_error = [&txn](blink::mojom::IDBTransaction::PutCallback callback,
                             blink::mojom::IDBException code,
                             const std::u16string& message) {
    txn->IncrementNumErrorsSent();
    std::move(callback).Run(
        blink::mojom::IDBTransactionPutResult::NewErrorResult(
            blink::mojom::IDBError::New(code, message)));
  };

  if (!connection()->database()->IsObjectStoreIdInMetadata(object_store_id)) {
    on_put_error(std::move(callback), blink::mojom::IDBException::kUnknownError,
                 u"Bad request");
    return Status::InvalidArgument("Invalid object_store_id.");
  }

  const IndexedDBObjectStoreMetadata& object_store =
      connection()->database()->GetObjectStoreMetadata(object_store_id);
  DCHECK(object_store.auto_increment || key.IsValid());
  if (put_mode != blink::mojom::IDBPutMode::CursorUpdate &&
      object_store.auto_increment && !key.IsValid()) {
    IndexedDBKey auto_inc_key = GenerateAutoIncrementKey(object_store_id);
    key_was_generated = true;
    if (!auto_inc_key.IsValid()) {
      on_put_error(std::move(callback),
                   blink::mojom::IDBException::kConstraintError,
                   u"Maximum key generator value reached.");
      return Status::OK();
    }
    key = std::move(auto_inc_key);
  }

  if (!key.IsValid()) {
    return Status::InvalidArgument("Invalid key");
  }

  if (put_mode == blink::mojom::IDBPutMode::AddOnly) {
    ASSIGN_OR_RETURN(
        std::optional<BackingStore::RecordIdentifier> preexisting_record,
        BackingStoreTransaction()->KeyExistsInObjectStore(object_store_id,
                                                          key));
    if (preexisting_record) {
      on_put_error(std::move(callback),
                   blink::mojom::IDBException::kConstraintError,
                   u"Key already exists in the object store.");
      return Status::OK();
    }
  }

  std::vector<std::unique_ptr<IndexWriter>> index_writers;
  std::string error_message;
  bool obeys_constraints = false;
  bool backing_store_success = MakeIndexWriters(
      this, object_store, key, key_was_generated, std::move(index_keys),
      &index_writers, &error_message, &obeys_constraints);
  if (!backing_store_success) {
    on_put_error(std::move(callback), blink::mojom::IDBException::kUnknownError,
                 u"Internal error: backing store error updating index keys.");
    return Status::OK();
  }
  if (!obeys_constraints) {
    on_put_error(std::move(callback),
                 blink::mojom::IDBException::kConstraintError,
                 base::UTF8ToUTF16(error_message));
    return Status::OK();
  }

  // Before this point, don't do any mutation. After this point, rollback the
  // transaction in case of error.
  ASSIGN_OR_RETURN(BackingStore::RecordIdentifier new_record,
                   BackingStoreTransaction()->PutRecord(object_store_id, key,
                                                        std::move(value)));

  {
    TRACE_EVENT1("IndexedDB", "Database::PutOperation.UpdateIndexes", "txn.id",
                 id());
    for (const auto& writer : index_writers) {
      writer->WriteIndexKeys(new_record, BackingStoreTransaction(),
                             object_store_id);
    }
  }

  if (object_store.auto_increment &&
      put_mode != blink::mojom::IDBPutMode::CursorUpdate &&
      key.type() == blink::mojom::IDBKeyType::Number) {
    TRACE_EVENT1("IndexedDB", "Database::PutOperation.AutoIncrement", "txn.id",
                 id());
    // Maximum integer uniquely representable as ECMAScript number.
    const double max_generator_value = 9007199254740992.0;
    int64_t new_max = 1 + base::saturated_cast<int64_t>(floor(
                              std::min(key.number(), max_generator_value)));
    IDB_RETURN_IF_ERROR(
        BackingStoreTransaction()->MaybeUpdateKeyGeneratorCurrentNumber(
            object_store_id, new_max, key_was_generated));
  }
  {
    TRACE_EVENT1("IndexedDB", "Database::PutOperation.Callbacks", "txn.id",
                 id());
    std::move(callback).Run(
        blink::mojom::IDBTransactionPutResult::NewKey(std::move(key)));
  }

  bucket_context()->delegate().on_content_changed.Run(
      connection()->database()->name(), object_store.name);
  return Status::OK();
}

void Transaction::SetIndexKeys(int64_t object_store_id,
                               IndexedDBKey primary_key,
                               IndexedDBIndexKeys index_keys) {
  if (!IsAcceptingRequests() || !connection()->IsConnected()) {
    return;
  }

  if (!primary_key.IsValid()) {
    mojo::ReportBadMessage("SetIndexKeys used with invalid key.");
    return;
  }

  if (mode() != blink::mojom::IDBTransactionMode::VersionChange) {
    mojo::ReportBadMessage(
        "SetIndexKeys must be called from a version change transaction.");
    return;
  }

  ScheduleTask(blink::mojom::IDBTaskType::Preemptive,
               base::BindOnce(&Transaction::DoSetIndexKeys,
                              base::Unretained(this), object_store_id,
                              std::move(primary_key), std::move(index_keys)));
}

Status Transaction::DoSetIndexKeys(int64_t object_store_id,
                                   IndexedDBKey primary_key,
                                   IndexedDBIndexKeys index_keys,
                                   Transaction* transaction) {
  DCHECK_EQ(this, transaction);
  TRACE_EVENT1("IndexedDB", "Database::SetIndexKeysOperation", "txn.id",
               transaction->id());
  DCHECK_EQ(mode(), blink::mojom::IDBTransactionMode::VersionChange);

  ASSIGN_OR_RETURN(std::optional<BackingStore::RecordIdentifier> found_record,
                   BackingStoreTransaction()->KeyExistsInObjectStore(
                       object_store_id, primary_key));
  if (!found_record) {
    return Abort(
        DatabaseError(blink::mojom::IDBException::kUnknownError,
                      "Internal error setting index keys for object store."));
  }

  std::vector<std::unique_ptr<IndexWriter>> index_writers;
  std::string error_message;
  bool obeys_constraints = false;

  const IndexedDBObjectStoreMetadata& object_store_metadata =
      connection()->database()->GetObjectStoreMetadata(object_store_id);
  std::vector<IndexedDBIndexKeys> keys_vec;
  keys_vec.emplace_back(std::move(index_keys));
  bool backing_store_success = MakeIndexWriters(
      this, object_store_metadata, primary_key, false, std::move(keys_vec),
      &index_writers, &error_message, &obeys_constraints);
  if (!backing_store_success) {
    return Abort(DatabaseError(blink::mojom::IDBException::kUnknownError,
                               "Internal error: backing store error updating "
                               "index keys."));
  }
  if (!obeys_constraints) {
    return Abort(DatabaseError(blink::mojom::IDBException::kConstraintError,
                               error_message));
  }

  for (const auto& writer : index_writers) {
    IDB_RETURN_IF_ERROR(writer->WriteIndexKeys(
        *found_record, BackingStoreTransaction(), object_store_id));
  }
  return Status::OK();
}

void Transaction::SetIndexKeysDone() {
  if (!IsAcceptingRequests() || !connection()->IsConnected()) {
    return;
  }

  if (mode() != blink::mojom::IDBTransactionMode::VersionChange) {
    mojo::ReportBadMessage(
        "SetIndexKeysDone must be called from a version change transaction.");
    return;
  }

  ScheduleTask(blink::mojom::IDBTaskType::Preemptive,
               base::BindOnce([](Transaction* transaction) {
                 transaction->DidCompletePreemptiveEvent();
                 return Status::OK();
               }));
}

void Transaction::Commit(int64_t num_errors_handled) {
  if (!IsAcceptingRequests() || !connection()->IsConnected()) {
    return;
  }

  num_errors_handled_ = num_errors_handled;

  // Always allow empty or delete-only transactions.
  if (preliminary_size_estimate_ <= 0) {
    SetCommitFlag();
    return;
  }

  bucket_context()->CheckCanUseDiskSpace(
      preliminary_size_estimate_, base::BindOnce(&Transaction::OnQuotaCheckDone,
                                                 ptr_factory_.GetWeakPtr()));
}

void Transaction::OnQuotaCheckDone(bool allowed) {
  // May have disconnected while quota check was pending.
  if (!connection()->IsConnected()) {
    return;
  }

  if (allowed) {
    SetCommitFlag();
  } else {
    connection()->AbortTransactionAndTearDownOnError(
        this, DatabaseError(blink::mojom::IDBException::kQuotaError));
  }
}

uint64_t Transaction::CreateExternalObjects(
    blink::mojom::IDBValuePtr& value,
    std::vector<IndexedDBExternalObject>* external_objects) {
  // Should only be called if there are external objects to process.
  CHECK(!value->external_objects.empty());

  base::CheckedNumeric<uint64_t> total_blob_size = 0;
  external_objects->resize(value->external_objects.size());
  for (size_t i = 0; i < value->external_objects.size(); ++i) {
    auto& object = value->external_objects[i];
    switch (object->which()) {
      case blink::mojom::IDBExternalObject::Tag::kBlobOrFile: {
        blink::mojom::IDBBlobInfoPtr& info = object->get_blob_or_file();
        uint64_t size = info->size;
        total_blob_size += size;

        if (info->file) {
          DCHECK_NE(info->size, IndexedDBExternalObject::kUnknownSize);
          (*external_objects)[i] = IndexedDBExternalObject(
              std::move(info->blob), info->file->name, info->mime_type,
              info->file->last_modified, info->size);
        } else {
          (*external_objects)[i] = IndexedDBExternalObject(
              std::move(info->blob), info->mime_type, info->size);
        }
        break;
      }
      case blink::mojom::IDBExternalObject::Tag::kFileSystemAccessToken:
        (*external_objects)[i] = IndexedDBExternalObject(
            std::move(object->get_file_system_access_token()));
        break;
    }
  }
  return total_blob_size.ValueOrDie();
}

Status Transaction::BlobWriteComplete(
    BlobWriteResult result,
    storage::mojom::WriteBlobToFileResult error) {
  TRACE_EVENT0("IndexedDB", "Transaction::BlobWriteComplete");
  if (state_ == FINISHED) {  // aborted
    return Status::OK();
  }
  DCHECK_EQ(state_, COMMITTING);

  switch (result) {
    case BlobWriteResult::kFailure: {
      Status status = Abort(
          DatabaseError(blink::mojom::IDBException::kDataError,
                        base::ASCIIToUTF16(base::StringPrintf(
                            "Failed to write blobs (%s)",
                            WriteBlobToFileResultToString(error).c_str()))));
      if (!status.ok()) {
        bucket_context_->OnDatabaseError(database_.get(), status, {});
      }
      // The result is ignored.
      return Status::OK();
    }
    case BlobWriteResult::kRunPhaseTwoAsync:
      ScheduleTask(base::BindOnce(&CommitPhaseTwoProxy));
      bucket_context_->QueueRunTasks();
      return Status::OK();
    case BlobWriteResult::kRunPhaseTwoAndReturnResult: {
      return CommitPhaseTwo();
    }
  }
  NOTREACHED();
}

Status Transaction::DoPendingCommit() {
  TRACE_EVENT1("IndexedDB", "Transaction::DoPendingCommit", "txn.id", id());

  ResetTimeoutTimer();

  // In multiprocess ports, front-end may have requested a commit but
  // an abort has already been initiated asynchronously by the
  // back-end.
  if (state_ == FINISHED) {
    return Status::OK();
  }
  DCHECK_NE(state_, COMMITTING);

  is_commit_pending_ = true;

  // Front-end has requested a commit, but this transaction is blocked by
  // other transactions. The commit will be initiated when the transaction
  // coordinator unblocks this transaction.
  if (state_ != STARTED) {
    return Status::OK();
  }

  // Front-end has requested a commit, but there may be tasks like
  // create_index which are considered synchronous by the front-end
  // but are processed asynchronously.
  if (HasPendingTasks()) {
    return Status::OK();
  }

  // If a transaction is being committed but it has sent more errors to the
  // front end than have been handled at this point, the transaction should be
  // aborted as it is unknown whether or not any errors unaccounted for will be
  // properly handled.
  if (num_errors_sent_ != num_errors_handled_) {
    is_commit_pending_ = false;
    return Abort(DatabaseError(blink::mojom::IDBException::kUnknownError));
  }

  SetState(COMMITTING);

  Status s;
  if (!used_) {
    s = CommitPhaseTwo();
  } else {
    // CommitPhaseOne will call the callback synchronously if there are no blobs
    // to write.
    s = backing_store_transaction_->CommitPhaseOne(base::BindOnce(
        [](base::WeakPtr<Transaction> transaction, BlobWriteResult result,
           storage::mojom::WriteBlobToFileResult error) {
          if (!transaction) {
            return Status::OK();
          }
          return transaction->BlobWriteComplete(result, error);
        },
        ptr_factory_.GetWeakPtr()));
  }

  return s;
}

Status Transaction::CommitPhaseTwo() {
  // Abort may have been called just as the blob write completed.
  if (state_ == FINISHED) {
    return Status::OK();
  }

  DCHECK_EQ(state_, COMMITTING);

  std::optional scheduling_priority_at_last_state_change =
      scheduling_priority_at_last_state_change_;
  SetState(FINISHED);

  Status s;
  bool committed;
  if (!used_) {
    committed = true;
  } else {
    s = backing_store_transaction_->CommitPhaseTwo();

    // This measurement includes the time it takes to commit to the backing
    // store (i.e. LevelDB), not just the blobs.
    const base::TimeDelta active_time =
        base::Time::Now() - diagnostics_.start_time;

    switch (mode_) {
      case blink::mojom::IDBTransactionMode::ReadOnly:
        base::UmaHistogramMediumTimes(
            "WebCore.IndexedDB.Transaction.ReadOnly.TimeActive2", active_time);
        if (scheduling_priority_at_last_state_change == 0) {
          base::UmaHistogramMediumTimes(
              "WebCore.IndexedDB.Transaction.ReadOnly.TimeActive2.Foreground",
              active_time);
        }
        break;
      case blink::mojom::IDBTransactionMode::ReadWrite:
        base::UmaHistogramMediumTimes(
            "WebCore.IndexedDB.Transaction.ReadWrite.TimeActive2", active_time);
        if (scheduling_priority_at_last_state_change == 0) {
          base::UmaHistogramMediumTimes(
              "WebCore.IndexedDB.Transaction.ReadWrite.TimeActive2.Foreground",
              active_time);
        }
        break;
      case blink::mojom::IDBTransactionMode::VersionChange:
        base::UmaHistogramMediumTimes(
            "WebCore.IndexedDB.Transaction.VersionChange.TimeActive2",
            active_time);
        if (scheduling_priority_at_last_state_change == 0) {
          base::UmaHistogramMediumTimes(
              "WebCore.IndexedDB.Transaction.VersionChange.TimeActive2."
              "Foreground",
              active_time);
        }
        break;
      default:
        NOTREACHED();
    }

    committed = s.ok();
  }

  // Backing store resources (held via cursors) must be released
  // before script callbacks are fired, as the script callbacks may
  // release references and allow the backing store itself to be
  // released, and order is critical.
  CloseOpenCursors();
  backing_store_transaction_.reset();

  // Transactions must also be marked as completed before the
  // front-end is notified, as the transaction completion unblocks
  // operations like closing connections.
  locks_receiver_.locks.clear();

  if (committed) {
    {
      TRACE_EVENT1("IndexedDB",
                   "Transaction::CommitPhaseTwo.TransactionCompleteCallbacks",
                   "txn.id", id());
      connection()->callbacks()->OnComplete(*this);
    }

    if (mode() != blink::mojom::IDBTransactionMode::ReadOnly) {
      const bool did_sync =
          mode() == blink::mojom::IDBTransactionMode::VersionChange ||
          durability_ == blink::mojom::IDBTransactionDurability::Strict;
      bucket_context_->delegate().on_files_written.Run(did_sync);
    }
    return s;
  }

  DatabaseError error;
  if (s.IndicatesDiskFull()) {
    error =
        DatabaseError(blink::mojom::IDBException::kQuotaError,
                      "Encountered disk full while committing transaction.");
  } else {
    error = DatabaseError(blink::mojom::IDBException::kUnknownError,
                          "Internal error committing transaction.");
  }
  connection()->callbacks()->OnAbort(*this, error);
  return s;
}

std::tuple<Transaction::RunTasksResult, Status> Transaction::RunTasks() {
  TRACE_EVENT1("IndexedDB", "Transaction::RunTasks", "txn.id", id());

  DCHECK(!processing_event_queue_);

  // May have been aborted.
  if (aborted_) {
    return {RunTasksResult::kAborted, Status::OK()};
  }
  if (IsTaskQueueEmpty() && !is_commit_pending_) {
    return {RunTasksResult::kNotFinished, Status::OK()};
  }

  processing_event_queue_ = true;

  if (!backing_store_transaction_begun_) {
    backing_store_transaction_->Begin(std::move(locks_receiver_.locks));
    backing_store_transaction_begun_ = true;
  }

  bool run_preemptive_queue =
      !preemptive_task_queue_.empty() || pending_preemptive_events_ != 0;
  TaskQueue* task_queue =
      run_preemptive_queue ? &preemptive_task_queue_ : &task_queue_;
  while (!task_queue->empty() && state_ != FINISHED) {
    DCHECK(state_ == STARTED || state_ == COMMITTING) << state_;
    Operation task(task_queue->pop());
    Status result = std::move(task).Run(this);
    if (!run_preemptive_queue) {
      DCHECK(diagnostics_.tasks_completed < diagnostics_.tasks_scheduled);
      ++diagnostics_.tasks_completed;
      NotifyOfIdbInternalsRelevantChange();
    }
    if (!result.ok()) {
      processing_event_queue_ = false;
      return {
          RunTasksResult::kError,
          result,
      };
    }

    run_preemptive_queue =
        !preemptive_task_queue_.empty() || pending_preemptive_events_ != 0;
    // Event itself may change which queue should be processed next.
    task_queue = run_preemptive_queue ? &preemptive_task_queue_ : &task_queue_;
  }

  // If there are no pending tasks, we haven't already committed/aborted,
  // and the front-end requested a commit, it is now safe to do so.
  if (!HasPendingTasks() && state_ == STARTED && is_commit_pending_) {
    processing_event_queue_ = false;
    // This can delete |this|.
    Status result = DoPendingCommit();
    if (!result.ok()) {
      return {RunTasksResult::kError, result};
    }
  }

  // The transaction may have been aborted while processing tasks.
  if (state_ == FINISHED) {
    processing_event_queue_ = false;
    return {aborted_ ? RunTasksResult::kAborted : RunTasksResult::kCommitted,
            Status::OK()};
  }

  DCHECK(state_ == STARTED || state_ == COMMITTING) << state_;

  // Otherwise, start a timer in case the front-end gets wedged and never
  // requests further activity.
  if (!HasPendingTasks() && state_ == STARTED && g_inactivity_timeout_enabled) {
    timeout_timer_.Start(FROM_HERE, kInactivityTimeoutPollPeriod,
                         base::BindRepeating(&Transaction::TimeoutFired,
                                             ptr_factory_.GetWeakPtr()));
  }
  processing_event_queue_ = false;
  return {RunTasksResult::kNotFinished, Status::OK()};
}

storage::mojom::IdbTransactionMetadataPtr Transaction::GetIdbInternalsMetadata()
    const {
  storage::mojom::IdbTransactionMetadataPtr info =
      storage::mojom::IdbTransactionMetadata::New();
  info->mode = static_cast<storage::mojom::IdbTransactionMode>(mode());
  switch (state()) {
    case Transaction::CREATED:
      info->state = storage::mojom::IdbTransactionState::kBlocked;
      break;
    case Transaction::STARTED:
      info->state = diagnostics().tasks_scheduled > 0
                        ? storage::mojom::IdbTransactionState::kRunning
                        : storage::mojom::IdbTransactionState::kStarted;
      break;
    case Transaction::COMMITTING:
      info->state = storage::mojom::IdbTransactionState::kCommitting;
      break;
    case Transaction::FINISHED:
      info->state = storage::mojom::IdbTransactionState::kFinished;
      break;
  }

  info->tid = id();
  info->connection_id = connection()->id();
  info->client_token = connection()->client_token().ToString();
  info->age =
      (base::Time::Now() - diagnostics().creation_time).InMillisecondsF();
  if (diagnostics().start_time.InMillisecondsSinceUnixEpoch() > 0) {
    info->runtime =
        (base::Time::Now() - diagnostics().start_time).InMillisecondsF();
  }
  info->tasks_scheduled = diagnostics().tasks_scheduled;
  info->tasks_completed = diagnostics().tasks_completed;

  for (int64_t id : scope()) {
    auto stores_it = database_->metadata().object_stores.find(id);
    if (stores_it != database_->metadata().object_stores.end()) {
      info->scope.emplace_back(stores_it->second.name);
    }
  }
  return info;
}

void Transaction::NotifyOfIdbInternalsRelevantChange() {
  // This metadata is included in the databases metadata, so call up the chain.
  if (database_) {
    database_->NotifyOfIdbInternalsRelevantChange();
  }
}

void Transaction::TimeoutFired() {
  if (!IsTransactionBlockingOtherClients(/*consider_priority=*/true)) {
    return;
  }

  if (++timeout_strikes_ >= kMaxTimeoutStrikes) {
    Status result =
        Abort(DatabaseError(blink::mojom::IDBException::kTimeoutError,
                            u"Transaction timed out due to inactivity."));
    if (!result.ok()) {
      bucket_context_->OnDatabaseError(database_.get(), result, {});
    }
    ResetTimeoutTimer();
  }
}

void Transaction::ResetTimeoutTimer() {
  timeout_timer_.Stop();
  timeout_strikes_ = 0;
}

void Transaction::SetState(State state) {
  state_ = state;
  if (connection_) {
    scheduling_priority_at_last_state_change_ =
        connection_->scheduling_priority();
  } else {
    scheduling_priority_at_last_state_change_ = std::nullopt;
  }
  NotifyOfIdbInternalsRelevantChange();
}

void Transaction::CloseOpenCursors() {
  TRACE_EVENT1("IndexedDB", "Transaction::CloseOpenCursors", "txn.id", id());

  // Cursor::Close() indirectly mutates |open_cursors_|, when it calls
  // Transaction::UnregisterOpenCursor().
  std::set<raw_ptr<Cursor, SetExperimental>> open_cursors =
      std::move(open_cursors_);
  open_cursors_.clear();
  for (Cursor* cursor : open_cursors) {
    cursor->Close();
  }
}

std::vector<PartitionedLockManager::PartitionedLockRequest>
Transaction::BuildLockRequests() const {
  // Locks for version change transactions are covered by `ConnectionRequest`.
  DCHECK_NE(mode(), blink::mojom::IDBTransactionMode::VersionChange);
  std::vector<PartitionedLockManager::PartitionedLockRequest> lock_requests;
  lock_requests.reserve(1 + scope().size());
  lock_requests.emplace_back(GetDatabaseLockId(database_->name()),
                             PartitionedLockManager::LockType::kShared);
  const auto object_store_lock_type =
      mode() == blink::mojom::IDBTransactionMode::ReadOnly
          ? PartitionedLockManager::LockType::kShared
          : PartitionedLockManager::LockType::kExclusive;
  for (int64_t object_store : scope()) {
    if (bucket_context_->ShouldUseSqlite()) {
      lock_requests.emplace_back(
          // TODO(crbug.com/40253999): this matches a constant in
          // indexed_db_leveldb_coding.cc. Refactor lock partitioning so the
          // constant isn't copied.
          PartitionedLockId{/*kObjectStoreLockPartition=*/1,
                            base::StringPrintf("%d|%d", object_store,
                                               database_->id_for_locks())},
          object_store_lock_type);
    } else {
      lock_requests.emplace_back(
          database_->backing_store_db()->GetLockId(object_store),
          object_store_lock_type);
    }
  }
  return lock_requests;
}

void Transaction::OnSchedulingPriorityUpdated(int new_priority) {
  auto* lock_request_data = static_cast<LockRequestData*>(
      locks_receiver_.GetUserData(LockRequestData::kKey));
  DCHECK(lock_request_data);
  lock_request_data->scheduling_priority = new_priority;
}

IndexedDBKey Transaction::GenerateAutoIncrementKey(int64_t object_store_id) {
  ASSIGN_OR_RETURN(
      int64_t current_number,
      BackingStoreTransaction()->GetKeyGeneratorCurrentNumber(object_store_id),
      [](auto) {
        LOG(ERROR) << "Failed to GetKeyGeneratorCurrentNumber";
        return IndexedDBKey();
      });
  // Maximum integer uniquely representable as ECMAScript number.
  const int64_t max_generator_value = 9007199254740992LL;
  if (current_number < 0 || current_number > max_generator_value) {
    return {};
  }

  return IndexedDBKey(current_number, blink::mojom::IDBKeyType::Number);
}

}  // namespace content::indexed_db
