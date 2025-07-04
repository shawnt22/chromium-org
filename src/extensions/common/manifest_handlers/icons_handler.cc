// Copyright 2014 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "extensions/common/manifest_handlers/icons_handler.h"

#include <memory>
#include <vector>

#include "base/files/file_util.h"
#include "base/lazy_instance.h"
#include "base/strings/escape.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/values.h"
#include "extensions/common/constants.h"
#include "extensions/common/extension.h"
#include "extensions/common/file_util.h"
#include "extensions/common/image_util.h"
#include "extensions/common/manifest_constants.h"
#include "extensions/common/manifest_handler_helpers.h"
#include "extensions/common/manifest_handlers/icon_variants_handler.h"
#include "extensions/strings/grit/extensions_strings.h"
#include "ui/gfx/geometry/size.h"

namespace extensions {

namespace keys = manifest_keys;

static base::LazyInstance<ExtensionIconSet>::DestructorAtExit g_empty_icon_set =
    LAZY_INSTANCE_INITIALIZER;

IconsHandler::IconsHandler() = default;
IconsHandler::~IconsHandler() = default;

// static
const ExtensionIconSet& IconsInfo::GetIcons(
    const Extension& extension,
    std::optional<ExtensionIconVariant::ColorScheme> color_scheme) {
  // Prefer `icon_variants` over `icons`.
  const IconVariantsInfo* icon_variants_info =
      IconVariantsInfo::GetIconVariants(extension);
  if (icon_variants_info) {
    return icon_variants_info->Get(color_scheme);
  }

  IconsInfo* info =
      static_cast<IconsInfo*>(extension.GetManifestData(keys::kIcons));
  return info ? info->icons : g_empty_icon_set.Get();
}

// static
ExtensionResource IconsInfo::GetIconResource(
    const Extension* extension,
    int size_in_px,
    ExtensionIconSet::Match match_type,
    ExtensionIconVariant::ColorScheme color_scheme) {
  const std::string& path =
      GetIcons(*extension, color_scheme).Get(size_in_px, match_type);
  return path.empty() ? ExtensionResource() : extension->GetResource(path);
}

// static
GURL IconsInfo::GetIconURL(const Extension* extension,
                           int size_in_px,
                           ExtensionIconSet::Match match_type,
                           ExtensionIconVariant::ColorScheme color_scheme) {
  const std::string& path =
      GetIcons(*extension, color_scheme).Get(size_in_px, match_type);
  return path.empty() ? GURL()
                      : extension->ResolveExtensionURL(base::EscapePath(path));
}

bool IconsHandler::Parse(Extension* extension, std::u16string* error) {
  std::unique_ptr<IconsInfo> icons_info(new IconsInfo);
  const base::Value::Dict* icons_dict =
      extension->manifest()->available_values().FindDict(keys::kIcons);
  if (!icons_dict) {
    *error = manifest_errors::kInvalidIcons;
    return false;
  }

  std::vector<std::string> warnings;
  if (!manifest_handler_helpers::LoadIconsFromDictionary(
          *icons_dict, &icons_info->icons, error, &warnings)) {
    return false;
  }
  for (const auto& warning : warnings) {
    extension->AddInstallWarning(InstallWarning(warning, keys::kIcons));
  }

  extension->SetManifestData(keys::kIcons, std::move(icons_info));
  return true;
}

bool IconsHandler::Validate(const Extension& extension,
                            std::string* error,
                            std::vector<InstallWarning>* warnings) const {
  // Analyze the icons for visibility using the default toolbar color, since
  // the majority of Chrome users don't modify their theme.
  return file_util::ValidateExtensionIconSet(IconsInfo::GetIcons(&extension),
                                             &extension, manifest_keys::kIcons,
                                             error);
}

base::span<const char* const> IconsHandler::Keys() const {
  static constexpr const char* kKeys[] = {keys::kIcons};
  return kKeys;
}

}  // namespace extensions
