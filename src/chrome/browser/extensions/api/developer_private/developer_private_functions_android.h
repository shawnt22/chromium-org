// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_EXTENSIONS_API_DEVELOPER_PRIVATE_DEVELOPER_PRIVATE_FUNCTIONS_ANDROID_H_
#define CHROME_BROWSER_EXTENSIONS_API_DEVELOPER_PRIVATE_DEVELOPER_PRIVATE_FUNCTIONS_ANDROID_H_

#include "chrome/common/extensions/api/developer_private.h"
#include "extensions/browser/extension_function.h"

DECLARE_UNIMPLEMENTED_EXTENSION_FUNCTION(DeveloperPrivateLoadDirectoryFunction,
                   "developerPrivate.loadDirectory",
                   DEVELOPERPRIVATE_LOADUNPACKEDCROS);
DECLARE_UNIMPLEMENTED_EXTENSION_FUNCTION(DeveloperPrivateRemoveMultipleExtensionsFunction,
                   "developerPrivate.removeMultipleExtensions",
                   DEVELOPERPRIVATE_REMOVEMULTIPLEEXTENSIONS);
DECLARE_UNIMPLEMENTED_EXTENSION_FUNCTION(
    DeveloperPrivateDismissMv2DeprecationNoticeForExtensionFunction,
    "developerPrivate.dismissMv2DeprecationNoticeForExtension",
    DEVELOPERPRIVATE_DISMISSMV2DEPRECATIONNOTICEFOREXTENSION);
DECLARE_UNIMPLEMENTED_EXTENSION_FUNCTION(DeveloperPrivateUploadExtensionToAccountFunction,
                   "developerPrivate.uploadExtensionToAccount",
                   DEVELOPERPRIVATE_UPLOADEXTENSIONTOACCOUNT);

#endif  // CHROME_BROWSER_EXTENSIONS_API_DEVELOPER_PRIVATE_DEVELOPER_PRIVATE_FUNCTIONS_ANDROID_H_
