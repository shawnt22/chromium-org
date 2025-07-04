// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "extensions/common/utils/content_script_utils.h"

#include <stddef.h>

#include <algorithm>
#include <memory>
#include <string_view>

#include "base/feature_list.h"
#include "base/files/file_util.h"
#include "base/strings/escape.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "content/public/common/url_constants.h"
#include "extensions/common/api/content_scripts.h"
#include "extensions/common/error_utils.h"
#include "extensions/common/manifest_constants.h"
#include "extensions/common/manifest_handlers/permissions_parser.h"
#include "extensions/common/mojom/match_origin_as_fallback.mojom-shared.h"
#include "extensions/common/permissions/permissions_data.h"
#include "extensions/common/url_pattern.h"
#include "extensions/common/url_pattern_set.h"
#include "extensions/common/user_script.h"
#include "extensions/strings/grit/extensions_strings.h"
#include "net/base/mime_util.h"
#include "third_party/blink/public/common/mime_util/mime_util.h"
#include "ui/base/l10n/l10n_util.h"
#include "url/gurl.h"

namespace extensions {

namespace errors = manifest_errors;

namespace script_parsing {

namespace {

BASE_FEATURE(kValidateContentScriptMimeType,
             "ValidateContentScriptMimeType",
             base::FEATURE_ENABLED_BY_DEFAULT);

size_t g_max_script_length_in_bytes = 1024u * 1024u * 500u;  // 500 MB.
size_t g_max_scripts_length_per_extension_in_bytes =
    1024u * 1024u * 1024u;  // 1 GB.

const char kEmptyFilesFynamicScriptError[] =
    "Script with ID '*' must specify at least one js or css file.";
constexpr char kEmptyMatchesDynamicScriptError[] =
    "Script with ID '*' must specify at least one match.";
constexpr char kInvalidExcludeMatchDynamicScriptError[] =
    "Script with ID '*' has invalid value for exclude_matches[*]: *";
constexpr char kInvalidMatchDynamicScriptError[] =
    "Script with ID '*' has invalid value for matches[*]: *";
constexpr char kForbiddenInlineCodeScriptError[] =
    "Script with ID '*' has forbidden inline code source";

// Returns true if the given path's mime type can be used for the given content
// script type.
bool IsMimeTypeValid(const base::FilePath& relative_path,
                     ContentScriptType content_script_type) {
  // TODO(https://crbug.com/40059598): Remove this if-check and always validate
  // the mime type in M139.
  if (!base::FeatureList::IsEnabled(kValidateContentScriptMimeType)) {
    return true;
  }

  auto file_extension = relative_path.Extension();
  if (file_extension.empty()) {
    return false;
  }

  // Strip leading ".".
  file_extension = file_extension.substr(1);

  // Allow .user.js files, which normally have no mime type, for JS content
  // scripts.
  if (content_script_type == ContentScriptType::kJs &&
      base::EqualsCaseInsensitiveASCII(file_extension,
                                       UserScript::kFileExtension)) {
    return true;
  }

  // Allow .scss files for CSS content scripts for compatibility with existing
  // extensions that were using such files before the mime type check was
  // introduced.
  if (content_script_type == ContentScriptType::kCss &&
      base::EqualsCaseInsensitiveASCII(file_extension, "scss")) {
    return true;
  }

  std::string mime_type;
  if (!net::GetWellKnownMimeTypeFromExtension(file_extension, &mime_type)) {
    return false;
  }

  switch (content_script_type) {
    case ContentScriptType::kJs:
      return blink::IsSupportedJavascriptMimeType(mime_type);

    case ContentScriptType::kCss:
      return mime_type == "text/css";
  }
}

// Returns false and sets the error if script file can't be loaded, or if it's
// not UTF-8 encoded. If a script file can be loaded but will exceed
// `max_script_length`, return true but add an install warning to `warnings`.
// Otherwise, decrement `remaining_length` by the script file's size.
bool IsScriptValid(const base::FilePath& path,
                   const base::FilePath& relative_path,
                   size_t max_script_length,
                   int file_not_read_error_id,
                   std::string* error,
                   std::vector<InstallWarning>* warnings,
                   size_t& remaining_length) {
  InstallWarning script_file_too_large_warning(
      l10n_util::GetStringFUTF8(IDS_EXTENSION_CONTENT_SCRIPT_FILE_TOO_LARGE,
                                relative_path.LossyDisplayName()),
      api::content_scripts::ManifestKeys::kContentScripts,
      base::UTF16ToUTF8(relative_path.LossyDisplayName()));
  if (remaining_length == 0u) {
    warnings->push_back(std::move(script_file_too_large_warning));
    return true;
  }

  if (!base::PathExists(path)) {
    *error = l10n_util::GetStringFUTF8(file_not_read_error_id,
                                       relative_path.LossyDisplayName());
    return false;
  }

  std::string content;
  bool read_successful =
      base::ReadFileToStringWithMaxSize(path, &content, max_script_length);
  // If the size of the file in `path` exceeds `max_script_length`,
  // ReadFileToStringWithMaxSize will return false but `content` will contain
  // the file's content truncated to `max_script_length`.
  if (!read_successful && content.length() != max_script_length) {
    *error = l10n_util::GetStringFUTF8(file_not_read_error_id,
                                       relative_path.LossyDisplayName());
    return false;
  }

  if (!base::IsStringUTF8(content)) {
    *error = l10n_util::GetStringFUTF8(IDS_EXTENSION_BAD_FILE_ENCODING,
                                       relative_path.LossyDisplayName());
    return false;
  }

  if (read_successful) {
    remaining_length -= content.size();
  } else {
    // Even though the script file is over the max size, we don't throw a hard
    // error so as not to break any existing extensions for which this is the
    // case.
    warnings->push_back(std::move(script_file_too_large_warning));
  }
  return true;
}

// Returns a string error when dynamic script with `script_id` or static script
// at `definition_index` has an empty field error.
std::u16string GetEmptyFieldError(std::string_view static_error,
                                  std::string_view dynamic_error,
                                  const std::string& script_id,
                                  std::optional<int> definition_index) {
  // Static scripts use a manifest error with `definition_index` since the
  // script id is autogenerated and the caller is unaware of it.
  if (definition_index) {
    return ErrorUtils::FormatErrorMessageUTF16(
        static_error, base::NumberToString(*definition_index));
  }

  return ErrorUtils::FormatErrorMessageUTF16(
      dynamic_error, UserScript::TrimPrefixFromScriptID(script_id));
}

// Returns a string error when dynamic script with `dynamic_error` and
// `script_id`, or static script with `static_error` at `definition_index` has
// an invalid exclude matches error.
std::u16string GetInvalidMatchError(std::string_view static_error,
                                    std::string_view dynamic_error,
                                    const std::string& script_id,
                                    std::optional<int> definition_index,
                                    URLPattern::ParseResult parse_result,
                                    int match_index) {
  std::string match_index_string = base::NumberToString(match_index);
  std::string parse_result_string =
      URLPattern::GetParseResultString(parse_result);

  // Static scripts use a manifest error with `definition_index` since the
  // script id is autogenerated and the caller is unaware of it.
  if (definition_index) {
    return ErrorUtils::FormatErrorMessageUTF16(
        static_error, base::NumberToString(*definition_index),
        match_index_string, parse_result_string);
  }

  return ErrorUtils::FormatErrorMessageUTF16(
      dynamic_error, UserScript::TrimPrefixFromScriptID(script_id),
      match_index_string, parse_result_string);
}

// Returns a string error when dynamic script with `script_id` or static script
// at `definition_index` has an empty matches error.
std::u16string GetEmptyMatchesError(const std::string& script_id,
                                    std::optional<int> definition_index) {
  return GetEmptyFieldError(errors::kInvalidMatchCount,
                            kEmptyMatchesDynamicScriptError, script_id,
                            definition_index);
}

// Returns a string error when dynamic script with `script_id` or static script
// at `definition_index` has an empty files error.
std::u16string GetEmptyFilesError(const std::string& script_id,
                                  std::optional<int> definition_index) {
  return GetEmptyFieldError(errors::kMissingFile, kEmptyFilesFynamicScriptError,
                            script_id, definition_index);
}

// Returns a string error when dynamic script with `script_id` or static script
// at `definition_index` has an invalid exclude matches error.
std::u16string GetInvalidExcludeMatchError(const std::string& script_id,
                                           std::optional<int> definition_index,
                                           URLPattern::ParseResult parse_result,
                                           int match_index) {
  return GetInvalidMatchError(errors::kInvalidExcludeMatch,
                              kInvalidExcludeMatchDynamicScriptError, script_id,
                              definition_index, parse_result, match_index);
}

// Returns a string error when dynamic script with `script_id` or static script
// at `definition_index` has an invalid matches error.
std::u16string GetInvalidMatchError(const std::string& script_id,
                                    std::optional<int> definition_index,
                                    URLPattern::ParseResult parse_result,
                                    int match_index) {
  return GetInvalidMatchError(errors::kInvalidMatch,
                              kInvalidMatchDynamicScriptError, script_id,
                              definition_index, parse_result, match_index);
}

}  // namespace

size_t GetMaxScriptLength() {
  return g_max_script_length_in_bytes;
}

size_t GetMaxScriptsLengthPerExtension() {
  return g_max_scripts_length_per_extension_in_bytes;
}

ScopedMaxScriptLengthOverride CreateScopedMaxScriptLengthForTesting(  // IN-TEST
    size_t max) {
  return ScopedMaxScriptLengthOverride(&g_max_script_length_in_bytes, max);
}

ScopedMaxScriptLengthOverride
CreateScopedMaxScriptsLengthPerExtensionForTesting(size_t max) {
  return ScopedMaxScriptLengthOverride(
      &g_max_scripts_length_per_extension_in_bytes, max);
}

bool ParseMatchPatterns(const std::vector<std::string>& matches,
                        const std::vector<std::string>* exclude_matches,
                        int creation_flags,
                        bool can_execute_script_everywhere,
                        bool all_urls_includes_chrome_urls,
                        std::optional<int> definition_index,
                        UserScript* result,
                        std::u16string* error,
                        bool* wants_file_access) {
  if (matches.empty()) {
    *error = GetEmptyMatchesError(result->id(), definition_index);
    return false;
  }

  const int valid_schemes =
      UserScript::ValidUserScriptSchemes(can_execute_script_everywhere);

  for (size_t i = 0; i < matches.size(); ++i) {
    URLPattern pattern(valid_schemes);

    const std::string& match_str = matches[i];
    URLPattern::ParseResult parse_result = pattern.Parse(match_str);
    if (parse_result != URLPattern::ParseResult::kSuccess) {
      *error =
          GetInvalidMatchError(result->id(), definition_index, parse_result, i);
      return false;
    }

    // TODO(aboxhall): check for webstore
    if (!all_urls_includes_chrome_urls &&
        pattern.scheme() != content::kChromeUIScheme) {
      // Exclude SCHEME_CHROMEUI unless it's been explicitly requested or
      // been granted by extension ID.
      // If the --extensions-on-chrome-urls flag has not been passed, requesting
      // a chrome:// url will cause a parse failure above, so there's no need to
      // check the flag here.
      pattern.SetValidSchemes(pattern.valid_schemes() &
                              ~URLPattern::SCHEME_CHROMEUI);
    }

    if (pattern.MatchesScheme(url::kFileScheme) &&
        !can_execute_script_everywhere) {
      if (wants_file_access)
        *wants_file_access = true;
      if (!(creation_flags & Extension::ALLOW_FILE_ACCESS)) {
        pattern.SetValidSchemes(pattern.valid_schemes() &
                                ~URLPattern::SCHEME_FILE);
      }
    }

    result->add_url_pattern(pattern);
  }

  if (exclude_matches) {
    for (size_t i = 0; i < exclude_matches->size(); ++i) {
      const std::string& match_str = exclude_matches->at(i);
      URLPattern pattern(valid_schemes);

      URLPattern::ParseResult parse_result = pattern.Parse(match_str);
      if (parse_result != URLPattern::ParseResult::kSuccess) {
        *error = GetInvalidExcludeMatchError(result->id(), definition_index,
                                             parse_result, i);
        return false;
      }

      result->add_exclude_url_pattern(pattern);
    }
  }

  return true;
}

bool ParseFileSources(
    const Extension* extension,
    const std::vector<api::scripts_internal::ScriptSource>* js,
    const std::vector<api::scripts_internal::ScriptSource>* css,
    std::optional<int> definition_index,
    UserScript* result,
    std::u16string* error) {
  if (js) {
    result->js_scripts().reserve(js->size());
    for (const auto& source : *js) {
      if (source.file) {
        GURL url =
            extension->ResolveExtensionURL(base::EscapePath(*source.file));
        ExtensionResource resource = extension->GetResource(*source.file);
        result->js_scripts().push_back(UserScript::Content::CreateFile(
            resource.extension_root(), resource.relative_path(), url));
      } else if (source.code) {
        // Inline code source is only allowed for user scripts.
        if (result->GetSource() != UserScript::Source::kDynamicUserScript) {
          *error = ErrorUtils::FormatErrorMessageUTF16(
              kForbiddenInlineCodeScriptError,
              UserScript::TrimPrefixFromScriptID(result->id()));
          return false;
        }

        GURL url = extension->ResolveExtensionURL(
            base::Uuid::GenerateRandomV4().AsLowercaseString());
        std::unique_ptr<UserScript::Content> content =
            UserScript::Content::CreateInlineCode(url);
        // TODO(crbug.com/40938420): This creates a copy of a
        // potentially-expensive string. Optimize the usage of inline code.
        content->set_content(*source.code);
        result->js_scripts().push_back(std::move(content));
      }
    }
  }

  if (css) {
    result->css_scripts().reserve(css->size());
    for (const auto& source : *css) {
      if (source.file) {
        GURL url =
            extension->ResolveExtensionURL(base::EscapePath(*source.file));
        ExtensionResource resource = extension->GetResource(*source.file);
        result->css_scripts().push_back(UserScript::Content::CreateFile(
            resource.extension_root(), resource.relative_path(), url));
      }
      // Note: We don't allow `code` in CSS blocks of any user script types yet.
    }
  }

  // The manifest needs to have at least one js or css user script definition.
  if (result->js_scripts().empty() && result->css_scripts().empty()) {
    *error = GetEmptyFilesError(result->id(), definition_index);
    return false;
  }

  return true;
}

void ParseGlobs(const std::vector<std::string>* include_globs,
                const std::vector<std::string>* exclude_globs,
                UserScript* result) {
  if (include_globs) {
    for (const std::string& glob : *include_globs) {
      result->add_glob(glob);
    }
  }

  if (exclude_globs) {
    for (const std::string& glob : *exclude_globs) {
      result->add_exclude_glob(glob);
    }
  }
}

bool ValidateMimeTypeFromFileExtension(const base::FilePath& relative_path,
                                       ContentScriptType content_script_type,
                                       std::string* error) {
  if (!IsMimeTypeValid(relative_path, content_script_type)) {
    switch (content_script_type) {
      case ContentScriptType::kJs:
        *error = l10n_util::GetStringFUTF8(
            IDS_EXTENSION_CONTENT_SCRIPT_FILE_BAD_JS_MIME_TYPE,
            relative_path.LossyDisplayName());
        return false;

      case ContentScriptType::kCss:
        *error = l10n_util::GetStringFUTF8(
            IDS_EXTENSION_CONTENT_SCRIPT_FILE_BAD_CSS_MIME_TYPE,
            relative_path.LossyDisplayName());
        return false;
    }
  }

  return true;
}

bool ValidateFileSources(const UserScriptList& scripts,
                         ExtensionResource::SymlinkPolicy symlink_policy,
                         std::string* error,
                         std::vector<InstallWarning>* warnings) {
  size_t remaining_scripts_length = GetMaxScriptsLengthPerExtension();

  for (const std::unique_ptr<UserScript>& script : scripts) {
    for (const std::unique_ptr<UserScript::Content>& js_script :
         script->js_scripts()) {
      // Don't validate scripts with inline code source, since they don't have
      // file sources.
      if (js_script->source() == UserScript::Content::Source::kInlineCode) {
        continue;
      }

      // Files with invalid mime types will be ignored.
      if (!ValidateMimeTypeFromFileExtension(js_script->relative_path(),
                                             ContentScriptType::kJs, error)) {
        warnings->emplace_back(
            base::StringPrintf(errors::kInvalidUserScriptMimeType,
                               error->c_str()),
            api::content_scripts::ManifestKeys::kContentScripts);
        continue;
      }

      const base::FilePath& path = ExtensionResource::GetFilePath(
          js_script->extension_root(), js_script->relative_path(),
          symlink_policy);
      size_t max_script_length =
          std::min(remaining_scripts_length, GetMaxScriptLength());
      if (!IsScriptValid(path, js_script->relative_path(), max_script_length,
                         IDS_EXTENSION_LOAD_JAVASCRIPT_FAILED, error, warnings,
                         remaining_scripts_length)) {
        return false;
      }
    }

    for (const std::unique_ptr<UserScript::Content>& css_script :
         script->css_scripts()) {
      // Files with invalid mime types will be ignored.
      if (!ValidateMimeTypeFromFileExtension(css_script->relative_path(),
                                             ContentScriptType::kCss, error)) {
        warnings->emplace_back(
            base::StringPrintf(errors::kInvalidUserScriptMimeType,
                               error->c_str()),
            api::content_scripts::ManifestKeys::kContentScripts);
        continue;
      }

      const base::FilePath& path = ExtensionResource::GetFilePath(
          css_script->extension_root(), css_script->relative_path(),
          symlink_policy);
      size_t max_script_length =
          std::min(remaining_scripts_length, GetMaxScriptLength());
      if (!IsScriptValid(path, css_script->relative_path(), max_script_length,
                         IDS_EXTENSION_LOAD_CSS_FAILED, error, warnings,
                         remaining_scripts_length)) {
        return false;
      }
    }
  }

  return true;
}

bool ValidateUserScriptMimeTypesFromFileExtensions(const UserScript& script,
                                                   std::string* error) {
  for (const std::unique_ptr<UserScript::Content>& js_script :
       script.js_scripts()) {
    // Don't validate scripts with inline code source, since they don't have
    // file sources.
    if (js_script->source() == UserScript::Content::Source::kInlineCode) {
      continue;
    }

    if (!ValidateMimeTypeFromFileExtension(js_script->relative_path(),
                                           ContentScriptType::kJs, error)) {
      return false;
    }
  }

  for (const std::unique_ptr<UserScript::Content>& css_script :
       script.css_scripts()) {
    if (!ValidateMimeTypeFromFileExtension(css_script->relative_path(),
                                           ContentScriptType::kCss, error)) {
      return false;
    }
  }

  return true;
}

bool ValidateMatchOriginAsFallback(
    mojom::MatchOriginAsFallbackBehavior match_origin_as_fallback,
    const URLPatternSet& url_patterns,
    std::u16string* error_out) {
  // If the extension is using `match_origin_as_fallback`, we require the
  // pattern to match all paths. This is because origins don't have a path;
  // thus, if an extension specified `"match_origin_as_fallback": true` for
  // a pattern of `"https://google.com/maps/*"`, this script would also run
  // on about:blank, data:, etc frames from https://google.com (because in
  // both cases, the precursor origin is https://google.com).
  if (match_origin_as_fallback ==
      mojom::MatchOriginAsFallbackBehavior::kAlways) {
    for (const auto& pattern : url_patterns) {
      if (pattern.path() != "/*") {
        *error_out = errors::kMatchOriginAsFallbackCantHavePaths;
        return false;
      }
    }
  }

  return true;
}

ExtensionResource::SymlinkPolicy GetSymlinkPolicy(const Extension* extension) {
  if ((extension->creation_flags() & Extension::FOLLOW_SYMLINKS_ANYWHERE) !=
      0) {
    return ExtensionResource::FOLLOW_SYMLINKS_ANYWHERE;
  }

  return ExtensionResource::SYMLINKS_MUST_RESOLVE_WITHIN_ROOT;
}

}  // namespace script_parsing
}  // namespace extensions
