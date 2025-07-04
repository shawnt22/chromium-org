// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/public/browser/browser_context.h"

#include <stddef.h>
#include <stdint.h>

#include <algorithm>
#include <limits>
#include <memory>
#include <optional>
#include <unordered_set>
#include <utility>
#include <vector>

#include "base/base64.h"
#include "base/check_op.h"
#include "base/command_line.h"
#include "base/files/file_path.h"
#include "base/functional/bind.h"
#include "base/lazy_instance.h"
#include "base/memory/ptr_util.h"
#include "base/memory/weak_ptr.h"
#include "base/metrics/field_trial_params.h"
#include "base/notreached.h"
#include "base/trace_event/trace_event.h"
#include "base/unguessable_token.h"
#include "build/build_config.h"
#include "components/download/public/common/in_progress_download_manager.h"
#include "components/services/storage/privileged/mojom/indexed_db_control.mojom.h"
#include "content/browser/blob_storage/chrome_blob_storage_context.h"
#include "content/browser/browser_context_impl.h"
#include "content/browser/browsing_data/browsing_data_remover_impl.h"
#include "content/browser/child_process_host_impl.h"
#include "content/browser/child_process_security_policy_impl.h"
#include "content/browser/in_memory_federated_permission_context.h"
#include "content/browser/media/browser_feature_provider.h"
#include "content/browser/preloading/prefetch/prefetch_container.h"
#include "content/browser/preloading/prefetch/prefetch_service.h"
#include "content/browser/preloading/prefetch/prefetch_type.h"
#include "content/browser/push_messaging/push_messaging_router.h"
#include "content/browser/site_info.h"
#include "content/browser/storage_partition_impl_map.h"
#include "content/public/browser/blob_handle.h"
#include "content/public/browser/browser_task_traits.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/content_browser_client.h"
#include "content/public/browser/download_manager.h"
#include "content/public/browser/permission_controller.h"
#include "content/public/browser/prefetch_service_delegate.h"
#include "content/public/browser/preloading_trigger_type.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/site_instance.h"
#include "content/public/browser/storage_partition_config.h"
#include "content/public/common/content_client.h"
#include "content/public/common/content_switches.h"
#include "media/base/media_switches.h"
#include "media/capabilities/in_memory_video_decode_stats_db_impl.h"
#include "media/capabilities/video_decode_stats_db_impl.h"
#include "media/mojo/services/video_decode_perf_history.h"
#include "media/mojo/services/webrtc_video_perf_history.h"
#include "net/http/http_request_headers.h"
#include "storage/browser/blob/blob_storage_context.h"
#include "storage/browser/file_system/external_mount_points.h"
#include "third_party/blink/public/mojom/loader/referrer.mojom.h"
#include "third_party/blink/public/mojom/push_messaging/push_messaging.mojom.h"
#include "third_party/perfetto/include/perfetto/tracing/traced_proto.h"
#include "third_party/perfetto/include/perfetto/tracing/traced_value.h"
#include "url/gurl.h"

namespace content {

class PrefetchService;
class PrefetchServiceDelegate;

namespace {

using perfetto::protos::pbzero::ChromeBrowserContext;
using perfetto::protos::pbzero::ChromeTrackEvent;

base::WeakPtr<storage::BlobStorageContext> BlobStorageContextGetterForBrowser(
    scoped_refptr<ChromeBlobStorageContext> blob_context) {
  DCHECK_CURRENTLY_ON(BrowserThread::IO);
  return blob_context->context()->AsWeakPtr();
}

}  // namespace

BrowserContext::BrowserContext() {
  impl_ = base::WrapUnique(new BrowserContextImpl(this));
  TRACE_EVENT("shutdown", "BrowserContext::BrowserContext",
              ChromeTrackEvent::kChromeBrowserContext, *this);
  TRACE_EVENT_BEGIN("shutdown", "Browser.BrowserContext",
                    perfetto::Track::FromPointer(this),
                    ChromeTrackEvent::kChromeBrowserContext, *this);
}

BrowserContext::~BrowserContext() {
  TRACE_EVENT("shutdown", "BrowserContext::~BrowserContext",
              ChromeTrackEvent::kChromeBrowserContext, *this);

  // End for ASYNC event "Browser.BrowserContext".
  TRACE_EVENT_END("shutdown", perfetto::Track::FromPointer(this),
                  ChromeTrackEvent::kChromeBrowserContext, *this);

  impl_.reset();
}

DownloadManager* BrowserContext::GetDownloadManager() {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  return impl()->GetDownloadManager();
}

storage::ExternalMountPoints* BrowserContext::GetMountPoints() {
  return impl()->GetMountPoints();
}

BrowsingDataRemover* BrowserContext::GetBrowsingDataRemover() {
  return impl()->GetBrowsingDataRemover();
}

PermissionController* BrowserContext::GetPermissionController() {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  return impl()->GetPermissionController();
}

StoragePartition* BrowserContext::GetStoragePartition(
    SiteInstance* site_instance,
    bool can_create) {
  if (site_instance)
    DCHECK_EQ(this, site_instance->GetBrowserContext());

  auto partition_config = site_instance
                              ? site_instance->GetStoragePartitionConfig()
                              : StoragePartitionConfig::CreateDefault(this);
  return GetStoragePartition(partition_config, can_create);
}

StoragePartition* BrowserContext::GetStoragePartition(
    const StoragePartitionConfig& storage_partition_config,
    bool can_create) {
  if (IsOffTheRecord()) {
    // An off the record profile MUST only use in memory storage partitions.
    CHECK(storage_partition_config.in_memory());
  }

  return impl()->GetOrCreateStoragePartitionMap()->Get(storage_partition_config,
                                                       can_create);
}

StoragePartition* BrowserContext::GetStoragePartitionForUrl(
    const GURL& url,
    bool can_create) {
  auto storage_partition_config =
      SiteInfo::GetStoragePartitionConfigForUrl(this, url);

  return GetStoragePartition(storage_partition_config, can_create);
}

void BrowserContext::ForEachLoadedStoragePartition(
    base::FunctionRef<void(StoragePartition*)> fn) {
  StoragePartitionImplMap* partition_map = impl()->storage_partition_map();
  if (!partition_map)
    return;

  partition_map->ForEach(fn);
}

size_t BrowserContext::GetLoadedStoragePartitionCount() {
  StoragePartitionImplMap* partition_map = impl()->storage_partition_map();
  return partition_map ? partition_map->size() : 0;
}

void BrowserContext::AsyncObliterateStoragePartition(
    const std::string& partition_domain,
    base::OnceClosure on_gc_required,
    base::OnceClosure done_callback) {
  impl()->GetOrCreateStoragePartitionMap()->AsyncObliterate(
      partition_domain, std::move(on_gc_required), std::move(done_callback));
}

void BrowserContext::GarbageCollectStoragePartitions(
    std::unordered_set<base::FilePath> active_paths,
    base::OnceClosure done) {
  impl()->GetOrCreateStoragePartitionMap()->GarbageCollect(
      std::move(active_paths), std::move(done));
}

StoragePartition* BrowserContext::GetDefaultStoragePartition() {
  return GetStoragePartition(StoragePartitionConfig::CreateDefault(this));
}

std::unique_ptr<content::PrefetchHandle>
BrowserContext::StartBrowserPrefetchRequest(
    const GURL& url,
    const std::string& embedder_histogram_suffix,
    bool javascript_enabled,
    std::optional<net::HttpNoVarySearchData> no_vary_search_hint,
    std::optional<PrefetchPriority> priority,
    const net::HttpRequestHeaders& additional_headers,
    std::unique_ptr<PrefetchRequestStatusListener> request_status_listener,
    base::TimeDelta ttl,
    bool should_append_variations_header,
    bool should_disable_block_until_head_timeout) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  TRACE_EVENT0("loading", "BrowserContext::StartBrowserPrefetchRequest");

  PrefetchService* prefetch_service =
      BrowserContextImpl::From(this)->GetPrefetchService();
  if (!prefetch_service) {
    if (request_status_listener) {
      request_status_listener->OnPrefetchStartFailedGeneric();
    }
    return nullptr;
  }

  PrefetchType prefetch_type(PreloadingTriggerType::kEmbedder,
                             /*use_prefetch_proxy=*/false);
  auto container = std::make_unique<PrefetchContainer>(
      this, url, prefetch_type, embedder_histogram_suffix,
      blink::mojom::Referrer(), javascript_enabled,
      /*referring_origin=*/std::nullopt, std::move(no_vary_search_hint),
      std::move(priority),
      /*attempt=*/nullptr, additional_headers,
      std::move(request_status_listener), ttl, should_append_variations_header,
      should_disable_block_until_head_timeout);
  return prefetch_service->AddPrefetchContainerWithHandle(std::move(container));
}

void BrowserContext::UpdatePrefetchServiceDelegateAcceptLanguageHeader(
    std::string accept_language_header) {
  PrefetchService* prefetch_service =
      BrowserContextImpl::From(this)->GetPrefetchService();
  if (!prefetch_service) {
    return;
  }
  prefetch_service->GetPrefetchServiceDelegate()->SetAcceptLanguageHeader(
      accept_language_header);
}

bool BrowserContext::IsPrefetchDuplicate(
    GURL& url,
    std::optional<net::HttpNoVarySearchData> no_vary_search_hint) {
  PrefetchService* prefetch_service =
      BrowserContextImpl::From(this)->GetPrefetchService();
  // `CHECK` is used here because this method should not be called unless there
  // is a `prefetch_service` created for `this` browser context.
  CHECK(prefetch_service);
  return prefetch_service->IsPrefetchDuplicate(url, no_vary_search_hint);
}

void BrowserContext::CreateMemoryBackedBlob(base::span<const uint8_t> data,
                                            const std::string& content_type,
                                            BlobCallback callback) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  ChromeBlobStorageContext* blob_context =
      ChromeBlobStorageContext::GetFor(this);
  GetIOThreadTaskRunner({})->PostTaskAndReplyWithResult(
      FROM_HERE,
      base::BindOnce(&ChromeBlobStorageContext::CreateMemoryBackedBlob,
                     base::WrapRefCounted(blob_context), data, content_type),
      std::move(callback));
}

BrowserContext::BlobContextGetter BrowserContext::GetBlobStorageContext() {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  scoped_refptr<ChromeBlobStorageContext> chrome_blob_context =
      ChromeBlobStorageContext::GetFor(this);
  return base::BindRepeating(&BlobStorageContextGetterForBrowser,
                             chrome_blob_context);
}

mojo::PendingRemote<blink::mojom::Blob> BrowserContext::GetBlobRemote(
    const std::string& uuid) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  return ChromeBlobStorageContext::GetBlobRemote(this, uuid);
}

void BrowserContext::DeliverPushMessage(
    const GURL& origin,
    int64_t service_worker_registration_id,
    const std::string& message_id,
    std::optional<std::string> payload,
    base::OnceCallback<void(blink::mojom::PushEventStatus)> callback) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  PushMessagingRouter::DeliverMessage(
      this, origin, service_worker_registration_id, message_id,
      std::move(payload), std::move(callback));
}

void BrowserContext::FirePushSubscriptionChangeEvent(
    const GURL& origin,
    int64_t service_worker_registration_id,
    blink::mojom::PushSubscriptionPtr new_subscription,
    blink::mojom::PushSubscriptionPtr old_subscription,
    base::OnceCallback<void(blink::mojom::PushEventStatus)> callback) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  PushMessagingRouter::FireSubscriptionChangeEvent(
      this, origin, service_worker_registration_id, std::move(new_subscription),
      std::move(old_subscription), std::move(callback));
}

void BrowserContext::NotifyWillBeDestroyed() {
  impl()->NotifyWillBeDestroyed();
}

void BrowserContext::EnsureResourceContextInitialized() {
  // This will be enough to tickle initialization of BrowserContext if
  // necessary, which initializes ResourceContext. The reason we don't call
  // ResourceContext::InitializeResourceContext() directly here is that
  // ResourceContext initialization may call back into BrowserContext
  // and when that call returns it'll end rewriting its Impl. It will
  // end up rewriting the same value but this still causes a race condition.
  //
  // See http://crbug.com/115678.
  GetDefaultStoragePartition();
}

void BrowserContext::SaveSessionState() {
  StoragePartition* storage_partition = GetDefaultStoragePartition();

  storage_partition->GetCookieManagerForBrowserProcess()
      ->SetForceKeepSessionState();

  DOMStorageContextWrapper* dom_storage_context_proxy =
      static_cast<DOMStorageContextWrapper*>(
          storage_partition->GetDOMStorageContext());
  dom_storage_context_proxy->SetForceKeepSessionState();

  storage_partition->GetIndexedDBControl().SetForceKeepSessionState();
}

void BrowserContext::SetDownloadManagerForTesting(
    std::unique_ptr<DownloadManager> download_manager) {
  impl()->SetDownloadManagerForTesting(std::move(download_manager));  // IN-TEST
}

void BrowserContext::SetPermissionControllerForTesting(
    std::unique_ptr<PermissionController> permission_controller) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  DCHECK(permission_controller);
  impl()->SetPermissionControllerForTesting(  // IN-TEST
      std::move(permission_controller));
}

SharedCorsOriginAccessList* BrowserContext::GetSharedCorsOriginAccessList() {
  return impl()->shared_cors_origin_access_list();
}

void BrowserContext::ShutdownStoragePartitions() {
  impl()->ShutdownStoragePartitions();
}

bool BrowserContext::ShutdownStarted() {
  return impl()->ShutdownStarted();
}

const std::string& BrowserContext::UniqueId() {
  return impl()->UniqueId();
}

media::VideoDecodePerfHistory* BrowserContext::GetVideoDecodePerfHistory() {
  return impl()->GetVideoDecodePerfHistory();
}

media::WebrtcVideoPerfHistory* BrowserContext::GetWebrtcVideoPerfHistory() {
  return impl()->GetWebrtcVideoPerfHistory();
}

media::learning::LearningSession* BrowserContext::GetLearningSession() {
  return impl()->GetLearningSession();
}

std::unique_ptr<download::InProgressDownloadManager>
BrowserContext::RetrieveInProgressDownloadManager() {
  return nullptr;
}

void BrowserContext::WriteIntoTrace(
    perfetto::TracedProto<ChromeBrowserContext> proto) const {
  perfetto::WriteIntoTracedProto(std::move(proto), impl());
}

ResourceContext* BrowserContext::GetResourceContext() const {
  return impl()->GetResourceContext();
}

void BrowserContext::BackfillPopupHeuristicGrants(
    base::OnceCallback<void(bool)> callback) {
  return impl_->BackfillPopupHeuristicGrants(std::move(callback));
}

base::WeakPtr<BrowserContext> BrowserContext::GetWeakPtr() {
  return weak_factory_.GetWeakPtr();
}

//////////////////////////////////////////////////////////////////////////////
// The //content embedder can override the methods below to change or extend
// how the //content layer interacts with a BrowserContext.  The code below
// provides default implementations where appropriate.
//
// TODO(crbug.com/40169693): Migrate method definitions from this
// section into a separate BrowserContextDelegate class and a separate
// browser_context_delegate.cc source file.

FileSystemAccessPermissionContext*
BrowserContext::GetFileSystemAccessPermissionContext() {
  return nullptr;
}

ContentIndexProvider* BrowserContext::GetContentIndexProvider() {
  return nullptr;
}

bool BrowserContext::CanUseDiskWhenOffTheRecord() {
  return false;
}

variations::VariationsClient* BrowserContext::GetVariationsClient() {
  return nullptr;
}

std::unique_ptr<media::VideoDecodePerfHistory>
BrowserContext::CreateVideoDecodePerfHistory() {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  const char kUseInMemoryDBParamName[] = "db_in_memory";
  const bool kUseInMemoryDBDefault = false;
  bool use_in_memory_db = base::GetFieldTrialParamByFeatureAsBool(
      media::kMediaCapabilitiesWithParameters, kUseInMemoryDBParamName,
      kUseInMemoryDBDefault);

  std::unique_ptr<media::VideoDecodeStatsDB> stats_db;
  if (use_in_memory_db) {
    stats_db = std::make_unique<media::InMemoryVideoDecodeStatsDBImpl>(nullptr);
  } else {
    auto* db_provider =
        GetDefaultStoragePartition()->GetProtoDatabaseProvider();

    stats_db = media::VideoDecodeStatsDBImpl::Create(
        GetPath().Append(FILE_PATH_LITERAL("VideoDecodeStats")), db_provider);
  }

  return std::make_unique<media::VideoDecodePerfHistory>(
      std::move(stats_db), BrowserFeatureProvider::GetFactoryCB());
}

FederatedIdentityApiPermissionContextDelegate*
BrowserContext::GetFederatedIdentityApiPermissionContext() {
  return impl()->GetFederatedPermissionContext();
}

FederatedIdentityAutoReauthnPermissionContextDelegate*
BrowserContext::GetFederatedIdentityAutoReauthnPermissionContext() {
  return impl()->GetFederatedPermissionContext();
}

FederatedIdentityPermissionContextDelegate*
BrowserContext::GetFederatedIdentityPermissionContext() {
  return impl()->GetFederatedPermissionContext();
}

KAnonymityServiceDelegate* BrowserContext::GetKAnonymityServiceDelegate() {
  return nullptr;
}

OriginTrialsControllerDelegate*
BrowserContext::GetOriginTrialsControllerDelegate() {
  return nullptr;
}

#if BUILDFLAG(IS_ANDROID)
std::string BrowserContext::GetExtraHeadersForUrl(const GURL& url) {
  return std::string();
}
#endif  // BUILDFLAG(IS_ANDROID)

}  // namespace content
