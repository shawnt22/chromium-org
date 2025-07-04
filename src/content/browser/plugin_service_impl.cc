// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/plugin_service_impl.h"

#include <stddef.h>

#include <string>
#include <string_view>
#include <utility>

#include "base/command_line.h"
#include "base/compiler_specific.h"
#include "base/files/file_path.h"
#include "base/functional/bind.h"
#include "base/location.h"
#include "base/logging.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/synchronization/waitable_event.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/thread_pool.h"
#include "base/threading/thread.h"
#include "build/build_config.h"
#include "content/browser/child_process_security_policy_impl.h"
#include "content/browser/plugin_list.h"
#include "content/browser/process_lock.h"
#include "content/browser/renderer_host/render_process_host_impl.h"
#include "content/browser/renderer_host/render_view_host_impl.h"
#include "content/common/content_switches_internal.h"
#include "content/public/browser/browser_task_traits.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/content_browser_client.h"
#include "content/public/browser/plugin_service_filter.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/resource_context.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/content_client.h"
#include "content/public/common/content_constants.h"
#include "content/public/common/content_plugin_info.h"
#include "content/public/common/content_switches.h"
#include "content/public/common/process_type.h"
#include "content/public/common/webplugininfo.h"
#include "services/metrics/public/cpp/ukm_builders.h"

namespace content {

// static
PluginService* PluginService::GetInstance() {
  return PluginServiceImpl::GetInstance();
}

void PluginService::PurgePluginListCache(BrowserContext* browser_context,
                                         bool reload_pages) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  for (RenderProcessHost::iterator it = RenderProcessHost::AllHostsIterator();
       !it.IsAtEnd(); it.Advance()) {
    RenderProcessHost* host = it.GetCurrentValue();
    if (!browser_context || host->GetBrowserContext() == browser_context)
      host->GetRendererInterface()->PurgePluginListCache(reload_pages);
  }
}

// static
PluginServiceImpl* PluginServiceImpl::GetInstance() {
  return base::Singleton<PluginServiceImpl>::get();
}

PluginServiceImpl::PluginServiceImpl() = default;

PluginServiceImpl::~PluginServiceImpl() = default;

void PluginServiceImpl::Init() {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  RegisterPlugins();
}

bool PluginServiceImpl::GetPluginInfoArray(
    const GURL& url,
    const std::string& mime_type,
    bool allow_wildcard,
    std::vector<WebPluginInfo>* plugins,
    std::vector<std::string>* actual_mime_types) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  return PluginList::Singleton()->GetPluginInfoArray(
      url, mime_type, allow_wildcard, plugins, actual_mime_types);
}

bool PluginServiceImpl::GetPluginInfo(content::BrowserContext* browser_context,
                                      const GURL& url,
                                      const std::string& mime_type,
                                      bool allow_wildcard,
                                      bool* is_stale,
                                      WebPluginInfo* info,
                                      std::string* actual_mime_type) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  std::vector<WebPluginInfo> plugins;
  std::vector<std::string> mime_types;

  bool stale =
      GetPluginInfoArray(url, mime_type, allow_wildcard, &plugins, &mime_types);
  if (is_stale)
    *is_stale = stale;

  for (size_t i = 0; i < plugins.size(); ++i) {
    if (!filter_ || filter_->IsPluginAvailable(browser_context, plugins[i])) {
      *info = plugins[i];
      if (actual_mime_type)
        *actual_mime_type = mime_types[i];
      return true;
    }
  }
  return false;
}

bool PluginServiceImpl::GetPluginInfoByPath(const base::FilePath& plugin_path,
                                            WebPluginInfo* info) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  std::vector<WebPluginInfo> plugins;
  PluginList::Singleton()->GetPluginsNoRefresh(&plugins);

  for (const WebPluginInfo& plugin : plugins) {
    if (plugin.path == plugin_path) {
      *info = plugin;
      return true;
    }
  }

  return false;
}

std::u16string PluginServiceImpl::GetPluginDisplayNameByPath(
    const base::FilePath& path) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  std::u16string plugin_name = path.LossyDisplayName();
  WebPluginInfo info;
  if (PluginService::GetInstance()->GetPluginInfoByPath(path, &info) &&
      !info.name.empty()) {
    plugin_name = info.name;
#if BUILDFLAG(IS_MAC)
    // Many plugins on the Mac have .plugin in the actual name, which looks
    // terrible, so look for that and strip it off if present.
    static constexpr std::u16string_view kPluginExtension = u".plugin";
    if (base::EndsWith(plugin_name, kPluginExtension))
      plugin_name.erase(plugin_name.size() - kPluginExtension.size());
#endif  // BUILDFLAG(IS_MAC)
  }
  return plugin_name;
}

void PluginServiceImpl::GetPlugins(GetPluginsCallback callback) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  // Run `callback` later, to stay compatible with prior behavior.
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(std::move(callback), GetPluginsSynchronous()));
}

std::vector<WebPluginInfo> PluginServiceImpl::GetPluginsSynchronous() {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  std::vector<WebPluginInfo> plugins;
  PluginList::Singleton()->GetPlugins(&plugins);
  return plugins;
}

void PluginServiceImpl::RegisterPlugins() {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  GetContentClient()->AddPlugins(&plugins_);
  for (const auto& plugin : plugins_)
    RegisterInternalPlugin(plugin.ToWebPluginInfo(), /*add_at_beginning=*/true);
}

// There should generally be very few plugins so a brute-force search is fine.
const ContentPluginInfo* PluginServiceImpl::GetRegisteredPluginInfo(
    const base::FilePath& plugin_path) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  for (auto& plugin : plugins_) {
    if (plugin.path == plugin_path)
      return &plugin;
  }
  return nullptr;
}

void PluginServiceImpl::SetFilter(PluginServiceFilter* filter) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  filter_ = filter;
}

PluginServiceFilter* PluginServiceImpl::GetFilter() {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  return filter_;
}

static const unsigned int kMaxCrashesPerInterval = 3;
static const unsigned int kCrashesInterval = 120;

void PluginServiceImpl::RegisterPluginCrash(const base::FilePath& path) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  auto i = crash_times_.find(path);
  if (i == crash_times_.end()) {
    crash_times_[path] = std::vector<base::Time>();
    i = crash_times_.find(path);
  }
  if (i->second.size() == kMaxCrashesPerInterval) {
    i->second.erase(i->second.begin());
  }
  base::Time time = base::Time::Now();
  i->second.push_back(time);
}

bool PluginServiceImpl::IsPluginUnstable(const base::FilePath& path) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  std::map<base::FilePath, std::vector<base::Time> >::const_iterator i =
      crash_times_.find(path);
  if (i == crash_times_.end()) {
    return false;
  }
  if (i->second.size() != kMaxCrashesPerInterval) {
    return false;
  }
  base::TimeDelta delta = base::Time::Now() - i->second[0];
  return delta.InSeconds() <= kCrashesInterval;
}

void PluginServiceImpl::RefreshPlugins() {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  PluginList::Singleton()->RefreshPlugins();
}

void PluginServiceImpl::RegisterInternalPlugin(
    const WebPluginInfo& info,
    bool add_at_beginning) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  PluginList::Singleton()->RegisterInternalPlugin(info, add_at_beginning);
}

void PluginServiceImpl::UnregisterInternalPlugin(const base::FilePath& path) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  PluginList::Singleton()->UnregisterInternalPlugin(path);
}

void PluginServiceImpl::GetInternalPlugins(
    std::vector<WebPluginInfo>* plugins) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  PluginList::Singleton()->GetInternalPlugins(plugins);
}

}  // namespace content
