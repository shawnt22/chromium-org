// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IOS_CHROME_BROWSER_TIPS_NOTIFICATIONS_MODEL_UTILS_H_
#define IOS_CHROME_BROWSER_TIPS_NOTIFICATIONS_MODEL_UTILS_H_

#import <UserNotifications/UserNotifications.h>

#import <optional>
#import <string_view>
#import <vector>

namespace base {
class TimeDelta;
}

enum class NotificationType;
class PrefService;

// Identifier for the tips notification.
extern NSString* const kTipsNotificationId;

// Key for tips notification type in UserInfo dictionary.
extern NSString* const kNotificationTypeKey;

// Pref that stores which notifications have been sent.
extern const char kTipsNotificationsSentPref[];

// Pref that stores which notification type was sent last.
extern const char kTipsNotificationsLastSent[];

// Pref that stores which notification type was triggered last.
extern const char kTipsNotificationsLastTriggered[];

// Pref that stores the last time that a notification was requested.
extern const char kTipsNotificationsLastRequestedTime[];

// Pref that stores the user's classification.
extern const char kTipsNotificationsUserType[];

// Pref that stores how many Tips notifications have been dismissed in a row.
extern const char kTipsNotificationsDismissCount[];

// Pref that stores how many Reactivation notifications were canceled because
// the user returned to the app before it triggered.
extern const char kReactivationNotificationsCanceledCount[];

// The type of Tips Notification, for an individual notification.
// Always keep this enum in sync with
// the corresponding IOSTipsNotificationType in enums.xml.
// LINT.IfChange
enum class TipsNotificationType {
  kDefaultBrowser = 0,
  kWhatsNew = 1,
  kSignin = 2,
  kError = 3,
  kSetUpListContinuation = 4,
  kDocking = 5,
  kOmniboxPosition = 6,
  kLens = 7,
  kEnhancedSafeBrowsing = 8,
  kLensOverlay = 9,
  kCPE = 10,
  kIncognitoLock = 11,
  kMaxValue = kIncognitoLock,
};
// LINT.ThenChange(/tools/metrics/histograms/metadata/ios/enums.xml)

// An enum to store a classification of Tips Notification users.
// LINT.IfChange
enum class TipsNotificationUserType {
  kUnknown = 0,
  kLessEngaged = 1,
  kActiveSeeker = 2,
  kMaxValue = kActiveSeeker,
};
// LINT.ThenChange(/tools/metrics/histograms/metadata/ios/enums.xml)

// Returns true if the given `notification` is a Tips notification.
bool IsTipsNotification(UNNotificationRequest* request);

// Returns true if the given `notification` is a Proactive Tips
// (AKA Reactivation) notification.
bool IsProactiveTipsNotification(UNNotificationRequest* request);

// Returns a userInfo dictionary pre-filled with the notification `type`.
NSDictionary* UserInfoForTipsNotificationType(TipsNotificationType type,
                                              bool for_reactivation,
                                              std::string_view profile_name);

// Returns the notification type found in a notification's userInfo dictionary.
std::optional<TipsNotificationType> ParseTipsNotificationType(
    UNNotificationRequest* request);

// Returns the notification content for a given Tips notification type.
UNNotificationContent* ContentForTipsNotificationType(
    TipsNotificationType type,
    bool for_reactivation,
    std::string_view profile_name);

// Returns the time delta used to trigger Tips notifications.
base::TimeDelta TipsNotificationTriggerDelta(
    bool for_reactivation,
    TipsNotificationUserType user_type);

// Returns a bitfield indicating which types of notifications should be
// enabled. Bits are assigned based on the enum `TipsNotificationType`.
int TipsNotificationsEnabledBitfield();

// Returns an ordered array containing the types of Tips Notifications to send.
// `for_reactivation` specifies whether to get the order for Reactivation
// notifications.
std::vector<TipsNotificationType> TipsNotificationsTypesOrder(
    bool for_reactivation);

// Returns the matching NotificationType for the TipsNotificationType `type`.
NotificationType NotificationTypeForTipsNotificationType(
    TipsNotificationType type);

// Returns the type of Tips Notification that is forced to be sent, via
// experimental settings.
std::optional<TipsNotificationType> ForcedTipsNotificationType();

// Returns the trigger time (in seconds) that was set in Experimental Settings.
// Returns 0 if it was not set.
int TipsNotificationTriggerExperimentalSetting();

// Returns the type indicating how the user was classified.
TipsNotificationUserType GetTipsNotificationUserType(PrefService* local_state);

// Sets the user's classification in local state prefs.
void SetTipsNotificationUserType(PrefService* local_state,
                                 TipsNotificationUserType user_type);

#endif  // IOS_CHROME_BROWSER_TIPS_NOTIFICATIONS_MODEL_UTILS_H_
