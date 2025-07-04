// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IOS_CHROME_COMMON_APP_GROUP_APP_GROUP_COMMAND_H_
#define IOS_CHROME_COMMON_APP_GROUP_APP_GROUP_COMMAND_H_

#import <UIKit/UIKit.h>

#import "base/ios/block_types.h"

typedef void (^URLOpenerBlock)(NSURL* URL);

// This class contains helper functions to prepare dictionary commands, place
// them in the shared NSUserDefault, and launch Chrome to execute them.
@interface AppGroupCommand : NSObject
- (instancetype)initWithSourceApp:(NSString*)sourceApp
                   URLOpenerBlock:(URLOpenerBlock)opener;

// Prepares a command without argument.
- (void)prepareWithCommandID:(NSString*)commandID;

// Prepares a command to open `URL`.
- (void)prepareToOpenURL:(NSURL*)URL;

// Prepares a command to open `URL` in incognito.
- (void)prepareToOpenURLInIncognito:(NSURL*)URL;

// Prepares a command to open an item in a list.
// `URL` is the URL in the item, and `index` is the index of the item in the
// list.
- (void)prepareToOpenItem:(NSURL*)URL index:(NSNumber*)index;

// Prepares a command to search for `text`.
- (void)prepareToSearchText:(NSString*)text;

// Prepares a command to incognito search for `text`.
- (void)prepareToIncognitoSearchText:(NSString*)text;

// Prepares a command to search for `image`.
- (void)prepareToSearchImageData:(NSData*)imageData
                      completion:(ProceduralBlock)completion;

// Prepares a command to incognito search for `image`.
- (void)prepareToIncognitoSearchImageData:(NSData*)imageData
                               completion:(ProceduralBlock)completion;

// Launches the main app and execute the receiver.
- (void)executeInApp;

// Launches the main app and execute the receiver for a given `gaiaID`.
- (void)executeInAppWithGaiaID:(NSString*)gaiaID;

@end

#endif  // IOS_CHROME_COMMON_APP_GROUP_APP_GROUP_COMMAND_H_
