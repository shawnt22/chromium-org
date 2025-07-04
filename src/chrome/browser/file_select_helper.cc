// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/file_select_helper.h"

#include <stddef.h>

#include <memory>
#include <string>
#include <utility>

#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/memory/ptr_util.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/thread_pool.h"
#include "base/threading/hang_watcher.h"
#include "build/build_config.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/enterprise/connectors/common.h"
#include "chrome/browser/platform_util.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/ui/browser_dialogs.h"
#include "chrome/browser/ui/chrome_select_file_policy.h"
#include "chrome/grit/generated_resources.h"
#include "components/enterprise/buildflags/buildflags.h"
#include "components/enterprise/common/proto/connectors.pb.h"
#include "components/safe_browsing/buildflags.h"
#include "content/public/browser/browser_task_traits.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/file_select_listener.h"
#include "content/public/browser/global_routing_id.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/browser/web_contents.h"
#include "net/base/filename_util.h"
#include "net/base/mime_util.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/models/dialog_model.h"
#include "ui/shell_dialogs/selected_file_info.h"

#if BUILDFLAG(IS_CHROMEOS)
#include "chrome/browser/ash/file_manager/fileapi_util.h"
#include "content/public/browser/site_instance.h"
#endif

#if BUILDFLAG(SAFE_BROWSING_DOWNLOAD_PROTECTION)
#include "chrome/browser/safe_browsing/download_protection/download_protection_service.h"
#include "chrome/browser/safe_browsing/download_protection/download_protection_util.h"
#include "chrome/browser/safe_browsing/safe_browsing_service.h"
#endif

#if BUILDFLAG(IS_ANDROID)
#include "base/android/content_uri_utils.h"
#else
#include "chrome/browser/picture_in_picture/picture_in_picture_window_manager.h"
#include "chrome/browser/picture_in_picture/scoped_disallow_picture_in_picture.h"
#include "chrome/browser/picture_in_picture/scoped_tuck_picture_in_picture.h"
#endif  // BUILDFLAG(IS_ANDROID)

using blink::mojom::FileChooserFileInfo;
using blink::mojom::FileChooserFileInfoPtr;
using blink::mojom::FileChooserParams;
using blink::mojom::FileChooserParamsPtr;
using content::BrowserThread;
using content::WebContents;

DEFINE_LOCAL_ELEMENT_IDENTIFIER_VALUE(kCancelButtonId);

namespace {

void DeleteFiles(std::vector<base::FilePath> paths) {
  for (auto& file_path : paths)
    base::DeleteFile(file_path);
}

bool IsValidProfile(Profile* profile) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!profile) {
    return false;
  }
  // No profile manager in unit tests.
  if (!g_browser_process->profile_manager())
    return true;
  return g_browser_process->profile_manager()->IsValidProfile(profile);
}

#if BUILDFLAG(SAFE_BROWSING_DOWNLOAD_PROTECTION)
// Safe Browsing checks are only applied when `params->mode` is
// `kSave`, which is only for PPAPI requests.
bool IsDownloadAllowedBySafeBrowsing(
    safe_browsing::DownloadCheckResult result) {
  using Result = safe_browsing::DownloadCheckResult;
  switch (result) {
    // Only allow downloads that are marked as SAFE or UNKNOWN by SafeBrowsing.
    // All other types are going to be blocked. UNKNOWN could be the result of a
    // failed safe browsing ping.
    case Result::UNKNOWN:
    case Result::SAFE:
    case Result::ALLOWLISTED_BY_POLICY:
      return true;

    case Result::DANGEROUS:
    case Result::UNCOMMON:
    case Result::DANGEROUS_HOST:
    case Result::POTENTIALLY_UNWANTED:
    case Result::DANGEROUS_ACCOUNT_COMPROMISE:
      return false;

    // Safe Browsing should only return these results for client downloads, not
    // for PPAPI downloads.
    case Result::ASYNC_SCANNING:
    case Result::ASYNC_LOCAL_PASSWORD_SCANNING:
    case Result::BLOCKED_PASSWORD_PROTECTED:
    case Result::BLOCKED_TOO_LARGE:
    case Result::SENSITIVE_CONTENT_BLOCK:
    case Result::SENSITIVE_CONTENT_WARNING:
    case Result::DEEP_SCANNED_SAFE:
    case Result::PROMPT_FOR_SCANNING:
    case Result::PROMPT_FOR_LOCAL_PASSWORD_SCANNING:
    case Result::DEEP_SCANNED_FAILED:
    case Result::BLOCKED_SCAN_FAILED:
    case Result::IMMEDIATE_DEEP_SCAN:
      NOTREACHED();
  }
  NOTREACHED();
}

void InterpretSafeBrowsingVerdict(base::OnceCallback<void(bool)> recipient,
                                  safe_browsing::DownloadCheckResult result) {
  std::move(recipient).Run(IsDownloadAllowedBySafeBrowsing(result));
}
#endif  // BUILDFLAG(SAFE_BROWSING_DOWNLOAD_PROTECTION)

#if BUILDFLAG(IS_ANDROID)
std::u16string GetDisplayName(const base::FilePath& content_uri) {
  std::u16string display_name;
  if (!base::MaybeGetFileDisplayName(content_uri, &display_name)) {
    display_name = content_uri.BaseName().AsUTF16Unsafe();
  }
  return display_name;
}
#endif

}  // namespace

struct FileSelectHelper::ActiveDirectoryEnumeration {
  explicit ActiveDirectoryEnumeration(const std::u16string& display_name)
      : display_name_(display_name) {}

  std::unique_ptr<net::DirectoryLister> lister_;
  const std::u16string display_name_;
  std::vector<blink::mojom::NativeFileInfoPtr> results_;
};

FileSelectHelper::FileSelectHelper(Profile* profile)
    : profile_(profile),
      render_frame_host_(nullptr),
      web_contents_(nullptr),
      select_file_dialog_(),
      select_file_types_(),
      dialog_type_(ui::SelectFileDialog::SELECT_OPEN_FILE),
      dialog_mode_(FileChooserParams::Mode::kOpen) {}

FileSelectHelper::~FileSelectHelper() {
  // There may be pending file dialogs, we need to tell them that we've gone
  // away so they don't try and call back to us.
  if (select_file_dialog_)
    select_file_dialog_->ListenerDestroyed();
}

void FileSelectHelper::FileSelected(const ui::SelectedFileInfo& file,
                                    int /* index */) {
  if (IsValidProfile(profile_)) {
    base::FilePath path = file.file_path;
    if (dialog_mode_ != FileChooserParams::Mode::kUploadFolder)
      path = path.DirName();
    profile_->set_last_selected_directory(path);
  }

  if (!render_frame_host_) {
    RunFileChooserEnd();
    return;
  }

  if (dialog_type_ == ui::SelectFileDialog::SELECT_UPLOAD_FOLDER) {
    StartNewEnumeration(file.local_path,
                        base::FilePath(file.display_name).AsUTF16Unsafe());
    return;
  }

  std::vector<ui::SelectedFileInfo> files;
  files.push_back(file);

#if BUILDFLAG(IS_MAC)
  base::ThreadPool::PostTask(
      FROM_HERE,
      {base::MayBlock(), base::TaskShutdownBehavior::CONTINUE_ON_SHUTDOWN},
      base::BindOnce(&FileSelectHelper::ProcessSelectedFilesMac, this, files));
#else
  ConvertToFileChooserFileInfoList(files);
#endif  // BUILDFLAG(IS_MAC)
}

void FileSelectHelper::MultiFilesSelected(
    const std::vector<ui::SelectedFileInfo>& files) {
  if (!files.empty() && IsValidProfile(profile_)) {
    base::FilePath path = files[0].file_path;
    if (dialog_mode_ != FileChooserParams::Mode::kUploadFolder)
      path = path.DirName();
    profile_->set_last_selected_directory(path);
  }
#if BUILDFLAG(IS_MAC)
  base::ThreadPool::PostTask(
      FROM_HERE,
      {base::MayBlock(), base::TaskShutdownBehavior::CONTINUE_ON_SHUTDOWN},
      base::BindOnce(&FileSelectHelper::ProcessSelectedFilesMac, this, files));
#else
  ConvertToFileChooserFileInfoList(files);
#endif  // BUILDFLAG(IS_MAC)
}

void FileSelectHelper::FileSelectionCanceled() {
  RunFileChooserEnd();
}

void FileSelectHelper::StartNewEnumeration(const base::FilePath& path,
                                           const std::u16string& display_name) {
  base_dir_ = path;
  auto entry = std::make_unique<ActiveDirectoryEnumeration>(display_name);
  entry->lister_ = base::WrapUnique(new net::DirectoryLister(
      path, net::DirectoryLister::NO_SORT_RECURSIVE, this));
  entry->lister_->Start();
  directory_enumeration_ = std::move(entry);
}

void FileSelectHelper::OnListFile(
    const net::DirectoryLister::DirectoryListerData& data) {
  // Directory upload only cares about files.
  if (data.info.IsDirectory())
    return;

  std::vector<std::u16string> base_subdirs;
#if BUILDFLAG(IS_ANDROID)
  for (const auto& subdir : data.info.subdirs()) {
    base_subdirs.push_back(base::UTF8ToUTF16(subdir));
  }
#endif
  directory_enumeration_->results_.push_back(blink::mojom::NativeFileInfo::New(
      data.path, data.info.GetName().AsUTF16Unsafe(), std::move(base_subdirs)));
}

std::unique_ptr<ui::DialogModel> FileSelectHelper::CreateConfirmationDialog(
    const std::u16string& display_name,
    std::vector<FileChooserFileInfoPtr> selected_files,
    base::OnceCallback<void(std::vector<blink::mojom::FileChooserFileInfoPtr>)>
        callback) {
  // Split callback for ok, cancel.
  auto split_callback = base::SplitOnceCallback(std::move(callback));
  auto ok_callback = std::move(split_callback.first);
  // Split again for cancel and close.
  auto cancel_callbacks =
      base::SplitOnceCallback(std::move(split_callback.second));

  ui::DialogModel::Builder dialog_builder;
  dialog_builder
      .SetTitle(l10n_util::GetPluralStringFUTF16(IDS_CONFIRM_FILE_UPLOAD_TITLE,
                                                 selected_files.size()))
      .AddParagraph(ui::DialogModelLabel(l10n_util::GetStringFUTF16(
          IDS_CONFIRM_FILE_UPLOAD_TEXT, display_name)))
      .AddOkButton(
          base::BindOnce(std::move(ok_callback), std::move(selected_files)),
          ui::DialogModel::Button::Params().SetLabel(
              l10n_util::GetStringUTF16(IDS_CONFIRM_FILE_UPLOAD_OK_BUTTON)))
      .AddCancelButton(base::BindOnce(std::move(cancel_callbacks.first),
                                      std::vector<FileChooserFileInfoPtr>()),
                       ui::DialogModel::Button::Params().SetId(kCancelButtonId))
      .SetCloseActionCallback(
          base::BindOnce(std::move(cancel_callbacks.second),
                         std::vector<FileChooserFileInfoPtr>()))
      .SetInitiallyFocusedField(kCancelButtonId);
  return dialog_builder.Build();
}

void FileSelectHelper::OnListDone(int error) {
  if (!web_contents_) {
    // Web contents was destroyed under us (probably by closing the tab). We
    // must notify |listener_| and release our reference to
    // ourself. RunFileChooserEnd() performs this.
    RunFileChooserEnd();
    return;
  }

  // This entry needs to be cleaned up when this function is done.
  std::unique_ptr<ActiveDirectoryEnumeration> entry =
      std::move(directory_enumeration_);
  if (error) {
    FileSelectionCanceled();
    return;
  }

  std::vector<FileChooserFileInfoPtr> chooser_files;
  for (const auto& native_file : entry->results_) {
    chooser_files.push_back(
        FileChooserFileInfo::NewNativeFile(native_file->Clone()));
  }

  if (dialog_type_ == ui::SelectFileDialog::SELECT_UPLOAD_FOLDER) {
    auto model = CreateConfirmationDialog(
        entry->display_name_, std::move(chooser_files),
        base::BindOnce(&FileSelectHelper::PerformContentAnalysisIfNeeded,
                       this));
    chrome::ShowTabModal(std::move(model), web_contents_);
  } else {
    listener_->FileSelected(std::move(chooser_files), base_dir_,
                            FileChooserParams::Mode::kUploadFolder);
    listener_.reset();
    EnumerateDirectoryEnd();
  }
}

void FileSelectHelper::ConvertToFileChooserFileInfoList(
    const std::vector<ui::SelectedFileInfo>& files) {
  if (AbortIfWebContentsDestroyed())
    return;

#if BUILDFLAG(IS_CHROMEOS)
  if (!files.empty()) {
    if (!IsValidProfile(profile_)) {
      RunFileChooserEnd();
      return;
    }
    // Converts |files| into FileChooserFileInfo with handling of non-native
    // files.
    content::SiteInstance* site_instance =
        render_frame_host_->GetSiteInstance();
    storage::FileSystemContext* file_system_context =
        profile_->GetStoragePartition(site_instance)->GetFileSystemContext();
    file_manager::util::ConvertSelectedFileInfoListToFileChooserFileInfoList(
        file_system_context, render_frame_host_->GetLastCommittedOrigin(),
        files,
        base::BindOnce(&FileSelectHelper::PerformContentAnalysisIfNeeded,
                       this));
    return;
  }
#endif  // BUILDFLAG(IS_CHROMEOS)

  std::vector<FileChooserFileInfoPtr> chooser_files;
  for (const auto& file : files) {
    chooser_files.push_back(
        FileChooserFileInfo::NewNativeFile(blink::mojom::NativeFileInfo::New(
            file.local_path, base::FilePath(file.display_name).AsUTF16Unsafe(),
            std::vector<std::u16string>())));
  }

  PerformContentAnalysisIfNeeded(std::move(chooser_files));
}

void FileSelectHelper::PerformContentAnalysisIfNeeded(
    std::vector<FileChooserFileInfoPtr> list) {
  if (AbortIfWebContentsDestroyed())
    return;

#if BUILDFLAG(ENTERPRISE_CLOUD_CONTENT_ANALYSIS)
  enterprise_connectors::ContentAnalysisDelegate::Data data;
  if (enterprise_connectors::ContentAnalysisDelegate::IsEnabled(
          profile_, web_contents_->GetLastCommittedURL(), &data,
          enterprise_connectors::AnalysisConnector::FILE_ATTACHED)) {
    data.reason =
        enterprise_connectors::ContentAnalysisRequest::FILE_PICKER_DIALOG;
    data.paths.reserve(list.size());
    for (const auto& file : list) {
      if (file && file->is_native_file())
        data.paths.push_back(file->get_native_file()->file_path);
    }

    if (data.paths.empty()) {
      NotifyListenerAndEnd(std::move(list));
    } else {
      enterprise_connectors::ContentAnalysisDelegate::CreateForWebContents(
          web_contents_, std::move(data),
          base::BindOnce(&FileSelectHelper::ContentAnalysisCompletionCallback,
                         this, std::move(list)),
          safe_browsing::DeepScanAccessPoint::UPLOAD);
    }
  } else {
    NotifyListenerAndEnd(std::move(list));
  }
#else
  NotifyListenerAndEnd(std::move(list));
#endif  // BUILDFLAG(ENTERPRISE_CLOUD_CONTENT_ANALYSIS)
}

#if BUILDFLAG(ENTERPRISE_CLOUD_CONTENT_ANALYSIS)
void FileSelectHelper::ContentAnalysisCompletionCallback(
    std::vector<blink::mojom::FileChooserFileInfoPtr> list,
    const enterprise_connectors::ContentAnalysisDelegate::Data& data,
    enterprise_connectors::ContentAnalysisDelegate::Result& result) {
  if (AbortIfWebContentsDestroyed())
    return;

  DCHECK_EQ(data.text.size(), 0u);
  DCHECK_EQ(result.text_results.size(), 0u);
  DCHECK_EQ(data.paths.size(), result.paths_results.size());
  DCHECK_GE(list.size(), result.paths_results.size());

  // If the user chooses to upload a folder and the folder contains sensitive
  // files, block the entire folder and update `result` to reflect the block
  // verdict for all files scanned.
  if (dialog_type_ == ui::SelectFileDialog::SELECT_UPLOAD_FOLDER) {
    if (base::Contains(result.paths_results, false)) {
      list.clear();
      for (size_t index = 0; index < data.paths.size(); ++index) {
        result.paths_results[index] = false;
      }
    }
    // Early return for folder upload, regardless of list being empty or not.
    NotifyListenerAndEnd(std::move(list));
    return;
  }

  // For single or multiple file uploads, remove any files that did not pass the
  // deep scan. Non-native files are skipped.
  size_t i = 0;
  for (auto it = list.begin(); it != list.end();) {
    if ((*it)->is_native_file()) {
      if (!result.paths_results[i]) {
        it = list.erase(it);
      } else {
        ++it;
      }
      ++i;
    } else {
      // Skip non-native files by incrementing the iterator without changing `i`
      // so that no result is skipped.
      ++it;
    }
  }

  NotifyListenerAndEnd(std::move(list));
}
#endif  // BUILDFLAG(ENTERPRISE_CLOUD_CONTENT_ANALYSIS)

void FileSelectHelper::NotifyListenerAndEnd(
    std::vector<blink::mojom::FileChooserFileInfoPtr> list) {
  listener_->FileSelected(std::move(list), base_dir_, dialog_mode_);
  listener_.reset();

  // No members should be accessed from here on.
  RunFileChooserEnd();
}

void FileSelectHelper::DeleteTemporaryFiles() {
  base::ThreadPool::PostTask(
      FROM_HERE,
      {base::MayBlock(), base::TaskPriority::BEST_EFFORT,
       base::TaskShutdownBehavior::BLOCK_SHUTDOWN},
      base::BindOnce(&DeleteFiles, std::move(temporary_files_)));
}

void FileSelectHelper::CleanUp() {
  if (!temporary_files_.empty()) {
    DeleteTemporaryFiles();

    // Now that the temporary files have been scheduled for deletion, there
    // is no longer any reason to keep this instance around.
    Release();
  }
}

bool FileSelectHelper::AbortIfWebContentsDestroyed() {
  if (abort_on_missing_web_contents_in_tests_ &&
      (render_frame_host_ == nullptr || web_contents_ == nullptr)) {
    RunFileChooserEnd();
    return true;
  }

  return false;
}

void FileSelectHelper::SetFileSelectListenerForTesting(
    scoped_refptr<content::FileSelectListener> listener) {
  DCHECK(listener);
  DCHECK(!listener_);
  listener_ = std::move(listener);
}

void FileSelectHelper::DontAbortOnMissingWebContentsForTesting() {
  abort_on_missing_web_contents_in_tests_ = false;
}

std::unique_ptr<ui::SelectFileDialog::FileTypeInfo>
FileSelectHelper::GetFileTypesFromAcceptType(
    const std::vector<std::u16string>& accept_types) {
  auto base_file_type = std::make_unique<ui::SelectFileDialog::FileTypeInfo>();
  if (accept_types.empty())
    return base_file_type;

  // Create FileTypeInfo and pre-allocate for the first extension list.
  auto file_type =
      std::make_unique<ui::SelectFileDialog::FileTypeInfo>(*base_file_type);
  file_type->include_all_files = true;
  file_type->extensions.resize(1);
  std::vector<base::FilePath::StringType>* extensions =
      &file_type->extensions.back();

  // Find the corresponding extensions.
  int valid_type_count = 0;
  int description_id = 0;
  for (const auto& accept_type : accept_types) {
    size_t old_extension_size = extensions->size();
    if (accept_type[0] == '.') {
      // If the type starts with a period it is assumed to be a file extension
      // so we just have to add it to the list.
      base::FilePath::StringType ext =
          base::FilePath::FromUTF16Unsafe(accept_type).value();
      extensions->push_back(ext.substr(1));
    } else {
      if (!base::IsStringASCII(accept_type))
        continue;
      std::string ascii_type = base::UTF16ToASCII(accept_type);
      if (ascii_type == "image/*")
        description_id = IDS_IMAGE_FILES;
      else if (ascii_type == "audio/*")
        description_id = IDS_AUDIO_FILES;
      else if (ascii_type == "video/*")
        description_id = IDS_VIDEO_FILES;

      net::GetExtensionsForMimeType(ascii_type, extensions);
    }

    if (extensions->size() > old_extension_size)
      valid_type_count++;
  }

  // If no valid extension is added, bail out.
  if (valid_type_count == 0)
    return base_file_type;

  // Use a generic description "Custom Files" if either of the following is
  // true:
  // 1) There're multiple types specified, like "audio/*,video/*"
  // 2) There're multiple extensions for a MIME type without parameter, like
  //    "ehtml,shtml,htm,html" for "text/html". On Windows, the select file
  //    dialog uses the first extension in the list to form the description,
  //    like "EHTML Files". This is not what we want.
  if (valid_type_count > 1 ||
      (valid_type_count == 1 && description_id == 0 && extensions->size() > 1))
    description_id = IDS_CUSTOM_FILES;

  if (description_id) {
    file_type->extension_description_overrides.push_back(
        l10n_util::GetStringUTF16(description_id));
  }

  return file_type;
}

// static
void FileSelectHelper::RunFileChooser(
    content::RenderFrameHost* render_frame_host,
    scoped_refptr<content::FileSelectListener> listener,
    const FileChooserParams& params) {
  Profile* profile = Profile::FromBrowserContext(
      render_frame_host->GetProcess()->GetBrowserContext());

  // FileSelectHelper will keep itself alive until it sends the result
  // message.
  scoped_refptr<FileSelectHelper> file_select_helper(
      new FileSelectHelper(profile));
  file_select_helper->RunFileChooser(render_frame_host, std::move(listener),
                                     params.Clone());
}

// static
void FileSelectHelper::EnumerateDirectory(
    content::WebContents* tab,
    scoped_refptr<content::FileSelectListener> listener,
    const base::FilePath& path) {
  Profile* profile = Profile::FromBrowserContext(tab->GetBrowserContext());
  // FileSelectHelper will keep itself alive until it sends the result
  // message.
  scoped_refptr<FileSelectHelper> file_select_helper(
      new FileSelectHelper(profile));
  file_select_helper->EnumerateDirectoryImpl(tab, std::move(listener), path);
}

void FileSelectHelper::RunFileChooser(
    content::RenderFrameHost* render_frame_host,
    scoped_refptr<content::FileSelectListener> listener,
    FileChooserParamsPtr params) {
  DCHECK(!render_frame_host_);
  DCHECK(!web_contents_);
  DCHECK(listener);
  DCHECK(!listener_);
  DCHECK(params->default_file_name.empty() ||
         params->mode == FileChooserParams::Mode::kSave)
      << "The default_file_name parameter should only be specified for Save "
         "file choosers";
  DCHECK(params->default_file_name == params->default_file_name.BaseName())
      << "The default_file_name parameter should not contain path separators";

  render_frame_host_ = render_frame_host;
  web_contents_ = WebContents::FromRenderFrameHost(render_frame_host);
  listener_ = std::move(listener);
  content::WebContentsObserver::Observe(web_contents_);

#if !BUILDFLAG(IS_ANDROID)
  if (PictureInPictureWindowManager::GetInstance()
          ->ShouldFileDialogBlockPictureInPicture(web_contents_)) {
    scoped_disallow_picture_in_picture_ =
        std::make_unique<ScopedDisallowPictureInPicture>();
  } else if (PictureInPictureWindowManager::GetInstance()
                 ->ShouldFileDialogTuckPictureInPicture(web_contents_)) {
    scoped_tuck_picture_in_picture_ =
        std::make_unique<ScopedTuckPictureInPicture>();
  }
#endif  // !BUILDFLAG(IS_ANDROID)

  base::ThreadPool::PostTask(
      FROM_HERE, {base::MayBlock()},
      base::BindOnce(&FileSelectHelper::GetFileTypesInThreadPool, this,
                     std::move(params)));

  // Because this class returns notifications to the RenderViewHost, it is
  // difficult for callers to know how long to keep a reference to this
  // instance. We AddRef() here to keep the instance alive after we return
  // to the caller, until the last callback is received from the file dialog.
  // At that point, we must call RunFileChooserEnd().
  AddRef();
}

void FileSelectHelper::GetFileTypesInThreadPool(FileChooserParamsPtr params) {
  select_file_types_ = GetFileTypesFromAcceptType(params->accept_types);
  select_file_types_->allowed_paths =
      params->need_local_path ? ui::SelectFileDialog::FileTypeInfo::NATIVE_PATH
                              : ui::SelectFileDialog::FileTypeInfo::ANY_PATH;

  content::GetUIThreadTaskRunner({})->PostTask(
      FROM_HERE,
      base::BindOnce(&FileSelectHelper::GetSanitizedFilenameOnUIThread, this,
                     std::move(params)));
}

void FileSelectHelper::GetSanitizedFilenameOnUIThread(
    FileChooserParamsPtr params) {
  if (AbortIfWebContentsDestroyed())
    return;

  base::FilePath default_file_path = profile_->last_selected_directory().Append(
      GetSanitizedFileName(params->default_file_name));
#if BUILDFLAG(SAFE_BROWSING_DOWNLOAD_PROTECTION)
  // Mode `kSave` is only for PPAPI writes, which are checked by Safe Browsing.
  // See comments on
  // //third_party/blink/public/mojom/choosers/file_chooser.mojom.
  if (params->mode == FileChooserParams::Mode::kSave) {
    CheckDownloadRequestWithSafeBrowsing(default_file_path, std::move(params));
    return;
  }
#endif
  RunFileChooserOnUIThread(default_file_path, std::move(params));
}

#if BUILDFLAG(SAFE_BROWSING_DOWNLOAD_PROTECTION)
void FileSelectHelper::CheckDownloadRequestWithSafeBrowsing(
    const base::FilePath& default_file_path,
    FileChooserParamsPtr params) {
  safe_browsing::SafeBrowsingService* sb_service =
      g_browser_process->safe_browsing_service();

  if (!sb_service || !sb_service->download_protection_service() ||
      !sb_service->download_protection_service()->enabled()) {
    RunFileChooserOnUIThread(default_file_path, std::move(params));
    return;
  }

  std::vector<base::FilePath::StringType> alternate_extensions;
  if (select_file_types_) {
    for (const auto& extensions_list : select_file_types_->extensions) {
      for (const auto& extension_in_list : extensions_list) {
        base::FilePath::StringType extension =
            default_file_path.ReplaceExtension(extension_in_list)
                .FinalExtension();
        alternate_extensions.push_back(extension);
      }
    }
  }

  GURL requestor_url = params->requestor;
  sb_service->download_protection_service()->CheckPPAPIDownloadRequest(
      requestor_url, render_frame_host_, default_file_path,
      alternate_extensions, profile_,
      base::BindOnce(
          &InterpretSafeBrowsingVerdict,
          base::BindOnce(&FileSelectHelper::ProceedWithSafeBrowsingVerdict,
                         this, default_file_path, std::move(params))));
}

void FileSelectHelper::ProceedWithSafeBrowsingVerdict(
    const base::FilePath& default_file_path,
    FileChooserParamsPtr params,
    bool allowed_by_safe_browsing) {
  if (!allowed_by_safe_browsing) {
    RunFileChooserEnd();
    return;
  }
  RunFileChooserOnUIThread(default_file_path, std::move(params));
}
#endif  // BUILDFLAG(SAFE_BROWSING_DOWNLOAD_PROTECTION)

void FileSelectHelper::RunFileChooserOnUIThread(
    const base::FilePath& default_file_path,
    FileChooserParamsPtr params) {
  DCHECK(params);
  DCHECK(!select_file_dialog_);
  if (AbortIfWebContentsDestroyed())
    return;

  select_file_dialog_ = ui::SelectFileDialog::Create(
      this, std::make_unique<ChromeSelectFilePolicy>(web_contents_));
  if (!select_file_dialog_)
    return;

  dialog_mode_ = params->mode;
  switch (params->mode) {
    case FileChooserParams::Mode::kOpen:
      dialog_type_ = ui::SelectFileDialog::SELECT_OPEN_FILE;
      break;
    case FileChooserParams::Mode::kOpenMultiple:
      dialog_type_ = ui::SelectFileDialog::SELECT_OPEN_MULTI_FILE;
      break;
    case FileChooserParams::Mode::kUploadFolder:
      dialog_type_ = ui::SelectFileDialog::SELECT_UPLOAD_FOLDER;
      break;
    case FileChooserParams::Mode::kSave:
      dialog_type_ = ui::SelectFileDialog::SELECT_SAVEAS_FILE;
      break;
    default:
      NOTREACHED();
  }

  gfx::NativeWindow owning_window =
      platform_util::GetTopLevel(web_contents_->GetNativeView());

#if BUILDFLAG(IS_ANDROID)
  select_file_dialog_->SetAcceptTypes(params->accept_types);
  select_file_dialog_->SetUseMediaCapture(params->use_media_capture);
#endif

  // Never consider the current scope as hung. The hang watching deadline (if
  // any) is not valid since the user can take unbounded time to choose the
  // file.
  base::HangWatcher::InvalidateActiveExpectations();

  // 1-based index of default extension to show.
  int file_type_index =
      select_file_types_ && !select_file_types_->extensions.empty() ? 1 : 0;

  // TODO(https://crbug.com/340178601): this might go out of scope before
  // SelectFile() finishes - isn't this a potential UAF? is it ever actually
  // used?
  const GURL* caller =
      &render_frame_host_->GetMainFrame()->GetLastCommittedURL();

  select_file_dialog_->SelectFile(
      dialog_type_, params->title, default_file_path, select_file_types_.get(),
      file_type_index, base::FilePath::StringType(), owning_window, caller);

  select_file_types_.reset();
}

// This method is called when we receive the last callback from the file chooser
// dialog or if the renderer was destroyed. Perform any cleanup and release the
// reference we added in RunFileChooser().
void FileSelectHelper::RunFileChooserEnd() {
  // If there are temporary files, then this instance needs to stick around
  // until web_contents_ is destroyed, so that this instance can delete the
  // temporary files.
  if (!temporary_files_.empty())
    return;

  if (listener_)
    listener_->FileSelectionCanceled();
  render_frame_host_ = nullptr;
  web_contents_ = nullptr;
  // If the dialog was actually opened, dispose of our reference.
  if (select_file_dialog_) {
    select_file_dialog_->ListenerDestroyed();
    select_file_dialog_.reset();
  }

#if !BUILDFLAG(IS_ANDROID)
  scoped_disallow_picture_in_picture_.reset();
  scoped_tuck_picture_in_picture_.reset();
#endif  // !BUILDFLAG(IS_ANDROID)

  Release();
}

void FileSelectHelper::EnumerateDirectoryImpl(
    content::WebContents* tab,
    scoped_refptr<content::FileSelectListener> listener,
    const base::FilePath& path) {
  DCHECK(listener);
  DCHECK(!listener_);
  dialog_type_ = ui::SelectFileDialog::SELECT_NONE;
  web_contents_ = tab;
  listener_ = std::move(listener);
  // Because this class returns notifications to the RenderViewHost, it is
  // difficult for callers to know how long to keep a reference to this
  // instance. We AddRef() here to keep the instance alive after we return
  // to the caller, until the last callback is received from the enumeration
  // code. At that point, we must call EnumerateDirectoryEnd().
  AddRef();
#if BUILDFLAG(IS_ANDROID)
  if (path.IsContentUri()) {
    base::ThreadPool::PostTaskAndReplyWithResult(
        FROM_HERE, {base::MayBlock()}, base::BindOnce(&GetDisplayName, path),
        base::BindOnce(&FileSelectHelper::StartNewEnumeration, this, path));
    return;
  }
#endif
  StartNewEnumeration(path, path.BaseName().AsUTF16Unsafe());
}

// This method is called when we receive the last callback from the enumeration
// code. Perform any cleanup and release the reference we added in
// EnumerateDirectoryImpl().
void FileSelectHelper::EnumerateDirectoryEnd() {
  Release();
}

void FileSelectHelper::RenderFrameHostChanged(
    content::RenderFrameHost* old_host,
    content::RenderFrameHost* new_host) {
  // The |old_host| and its children are now pending deletion. Do not give them
  // file access past this point.
  for (content::RenderFrameHost* host = render_frame_host_; host;
       host = host->GetParentOrOuterDocument()) {
    if (host == old_host) {
      render_frame_host_ = nullptr;
      return;
    }
  }
}

void FileSelectHelper::RenderFrameDeleted(
    content::RenderFrameHost* render_frame_host) {
  if (render_frame_host == render_frame_host_)
    render_frame_host_ = nullptr;
}

void FileSelectHelper::WebContentsDestroyed() {
  render_frame_host_ = nullptr;
  web_contents_ = nullptr;
  profile_ = nullptr;
  CleanUp();
}

// static
bool FileSelectHelper::IsAcceptTypeValid(const std::string& accept_type) {
  // TODO(raymes): This only does some basic checks, extend to test more cases.
  // A 1 character accept type will always be invalid (either a "." in the case
  // of an extension or a "/" in the case of a MIME type).
  std::string unused;
  if (accept_type.length() <= 1 ||
      base::ToLowerASCII(accept_type) != accept_type ||
      base::TrimWhitespaceASCII(accept_type, base::TRIM_ALL, &unused) !=
          base::TRIM_NONE) {
    return false;
  }
  return true;
}

// static
base::FilePath FileSelectHelper::GetSanitizedFileName(
    const base::FilePath& suggested_filename) {
  if (suggested_filename.empty())
    return base::FilePath();
  return net::GenerateFileName(
      GURL(), std::string(), std::string(), suggested_filename.AsUTF8Unsafe(),
      std::string(), l10n_util::GetStringUTF8(IDS_DEFAULT_DOWNLOAD_FILENAME));
}
