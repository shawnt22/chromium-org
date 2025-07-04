// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IOS_CHROME_BROWSER_DOWNLOAD_MODEL_EXTERNAL_APP_UTIL_H_
#define IOS_CHROME_BROWSER_DOWNLOAD_MODEL_EXTERNAL_APP_UTIL_H_

@class NSString;
@class NSURL;

// iTunes Store item identifier for Google Drive app. Can be used with
// StoreKitCoordinator.
extern NSString* const kGoogleDriveITunesItemIdentifier;

// Custom URL scheme for Google Drive app. Can be used to check if Google Drive
// app is installed.
extern NSString* const kGoogleDriveAppURLScheme;

// Custom URL scheme for Google Maps app. Can be used to check if Google Maps
// app is installed.
extern NSString* const kGoogleMapsAppURLScheme;

// Bundle ID of Google Drive application.
extern NSString* const kGoogleDriveAppBundleID;

// Returns URL which can be used to check if Google Drive app is installed via
// -[UIApplication canOpenURL:] call.
NSURL* GetGoogleDriveAppURL();

// Returns URL which can be used to check if Google Maps app is installed via
// -[UIApplication canOpenURL:] call.
NSURL* GetGoogleMapsAppURL();

// Returns true if Google Drive app is installed.
bool IsGoogleDriveAppInstalled();

// Returns true if Google Maps app is installed.
bool IsGoogleMapsAppInstalled();

// Returns URL which can be used to open Chrome's directory in files.app.
// Returns nil if it cannot get the directory.
NSURL* GetFilesAppUrl();

#endif  // IOS_CHROME_BROWSER_DOWNLOAD_MODEL_EXTERNAL_APP_UTIL_H_
