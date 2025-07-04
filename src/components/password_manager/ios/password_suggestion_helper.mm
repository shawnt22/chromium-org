// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "components/password_manager/ios/password_suggestion_helper.h"

#import <utility>

#import "base/feature_list.h"
#import "base/not_fatal_until.h"
#import "base/strings/sys_string_conversions.h"
#import "base/time/time.h"
#import "components/autofill/core/common/form_data.h"
#import "components/autofill/core/common/password_form_fill_data.h"
#import "components/autofill/core/common/unique_ids.h"
#import "components/autofill/ios/browser/autofill_driver_ios.h"
#import "components/autofill/ios/browser/form_suggestion.h"
#import "components/password_manager/core/browser/features/password_features.h"
#import "components/password_manager/core/browser/password_manager_interface.h"
#import "components/password_manager/core/browser/password_ui_utils.h"
#import "components/password_manager/ios/account_select_fill_data.h"
#import "components/password_manager/ios/features.h"
#import "components/password_manager/ios/ios_password_manager_driver_factory.h"
#import "components/password_manager/ios/password_manager_ios_util.h"
#import "components/password_manager/ios/password_manager_java_script_feature.h"
#import "ios/web/public/js_messaging/web_frame.h"
#import "ios/web/public/js_messaging/web_frames_manager.h"
#import "ios/web/public/web_state.h"

using autofill::FieldRendererId;
using autofill::FormData;
using autofill::FormRendererId;
using autofill::PasswordFormFillData;
using base::SysNSStringToUTF16;
using base::SysNSStringToUTF8;
using base::SysUTF16ToNSString;
using base::SysUTF8ToNSString;
using password_manager::AccountSelectFillData;
using password_manager::FillData;
using password_manager::IsCrossOriginIframe;

namespace {

// Status of form extraction for a given frame.
enum class FormExtractionStatus {
  kNotRequested = 0,
  kRequested = 1,
  kCompleted = 2
};

// Gets the maximal period of time the form extraction queries can be hanging
// before timing out.
base::TimeDelta GetFormExtractionTimeoutMs() {
  return base::Milliseconds(
      password_manager::features::kIosPasswordFormExtractionRequestsTimeoutMs
          .Get());
}

// Gets the period of time between the scheduled cleanup tasks that completes
// the timed out form extraction queries. Set to be slightly more than
// GetFormExtractionTimeoutMs() to make sure that the hanging extraction query
// that originally triggered the cleanup task has the time to expire.
base::TimeDelta GetCleanupTaskPeriodMs() {
  return GetFormExtractionTimeoutMs() + base::Milliseconds(50);
}

}  // namespace

@protocol FillDataProvider <NSObject>

// True if suggestions are available for the field in form.
- (bool)
    areSuggestionsAvailableForFrameId:(NSString*)frameId
                       formRendererId:(autofill::FormRendererId)formRendererId
                      fieldRendererId:(autofill::FieldRendererId)fieldRendererId
                      isPasswordField:(bool)isPasswordField;

@end

// Represents a pending form query to be completed later with
// -runCompletion.
@interface PendingFormQuery : NSObject

// ID of the frame targeted by the query.
@property(nonatomic, strong, readonly) NSString* frameId;

// Timestamp when the query was created.
@property(nonatomic, readonly) base::TimeTicks creationTimestamp;

// Initializes the object with a `query` to complete with `completion` for
// frame with id `frameId`.
- (instancetype)initWithQuery:(FormSuggestionProviderQuery*)query
                   completion:(SuggestionsAvailableCompletion)completion
             fillDataProvider:(id<FillDataProvider>)fillDataProvider
              isPasswordField:(BOOL)isPasswordField;

// Runs the completion callback with the available fill data. This can only be
// done once in the lifetime of the query object.
- (void)runCompletion;

// Returns YES if the query is expired.
- (BOOL)isExpired;

@end

@implementation PendingFormQuery {
  FormSuggestionProviderQuery* _query;
  SuggestionsAvailableCompletion _completion;
  id<FillDataProvider> _fillDataProvider;
  BOOL _isPasswordField;
}

- (instancetype)initWithQuery:(FormSuggestionProviderQuery*)query
                   completion:(SuggestionsAvailableCompletion)completion
             fillDataProvider:(id<FillDataProvider>)fillDataProvider
              isPasswordField:(BOOL)isPasswordField {
  self = [super init];
  if (self) {
    _query = query;
    _completion = completion;
    _fillDataProvider = fillDataProvider;
    _frameId = query.frameID;
    _isPasswordField = isPasswordField;
    _creationTimestamp = base::TimeTicks::Now();
  }
  return self;
}

- (void)runCompletion {
  // Check that the completion was never run as -runCompletion
  // can only be called once.
  CHECK(_completion);

  _completion([_fillDataProvider
      areSuggestionsAvailableForFrameId:self.frameId
                         formRendererId:_query.formRendererID
                        fieldRendererId:_query.fieldRendererID
                        isPasswordField:_isPasswordField]);
  _completion = nil;
}

- (BOOL)isExpired {
  return base::TimeTicks::Now() >=
         _creationTimestamp + GetFormExtractionTimeoutMs();
}

@end

@interface PasswordSuggestionHelper () <FillDataProvider>

@end

@implementation PasswordSuggestionHelper {
  base::WeakPtr<web::WebState> _webState;

  // Fill data keyed by frame id for the frames' forms in the webstate.
  base::flat_map<std::string, std::unique_ptr<AccountSelectFillData>>
      _fillDataMap;

  // Pending form queries that are waiting for forms extraction results.
  NSMutableArray<PendingFormQuery*>* _pendingFormQueries;

  // Map of frame ids to the form extraction status for that frame.
  std::map<std::string, FormExtractionStatus> _framesFormExtractionStatus;

  // YES if there is pending queries cleanup task that was already scheduled.
  BOOL _cleanupScheduled;

  // Password Manager tied to the same web state as this helper.
  raw_ptr<password_manager::PasswordManagerInterface> _passwordManager;
}

#pragma mark - Initialization

- (instancetype)initWithWebState:(web::WebState*)webState
                 passwordManager:(password_manager::PasswordManagerInterface*)
                                     passwordManager {
  self = [super init];
  if (self) {
    _webState = webState->GetWeakPtr();
    _pendingFormQueries = [NSMutableArray array];
    _cleanupScheduled = NO;
    _passwordManager = passwordManager;
  }
  return self;
}

#pragma mark - Public methods

- (NSArray<FormSuggestion*>*)retrieveSuggestionsWithForm:
    (FormSuggestionProviderQuery*)formQuery {
  const std::string frameId = SysNSStringToUTF8(formQuery.frameID);
  AccountSelectFillData* fillData = [self fillDataForFrameId:frameId].first;

  BOOL isPasswordField =
      [self isPasswordFieldOnForm:formQuery
                         webFrame:[self frameWithId:frameId]];

  NSMutableArray<FormSuggestion*>* results = [NSMutableArray array];

  if (fillData->IsSuggestionsAvailable(formQuery.formRendererID,
                                       formQuery.fieldRendererID,
                                       isPasswordField)) {
    password_manager::FormInfoRetrievalResult formInfoResult =
        fillData->GetFormInfo(formQuery.formRendererID,
                              formQuery.fieldRendererID, isPasswordField);
    bool is_single_username_form =
        formInfoResult.has_value() &&
        formInfoResult.value()->username_element_id &&
        !formInfoResult.value()->password_element_id;

    std::vector<password_manager::UsernameAndRealm> usernameAndRealms =
        fillData->RetrieveSuggestions(formQuery.formRendererID,
                                      formQuery.fieldRendererID,
                                      isPasswordField);

    for (const auto& usernameAndRealm : usernameAndRealms) {
      NSString* username = SysUTF16ToNSString(usernameAndRealm.username);
      NSString* realm = nil;
      if (!usernameAndRealm.realm.empty()) {
        url::Origin origin = url::Origin::Create(GURL(usernameAndRealm.realm));
        realm = SysUTF8ToNSString(password_manager::GetShownOrigin(origin));
      }

      autofill::SuggestionType suggestionType =
          usernameAndRealm.is_backup_credential
              ? autofill::SuggestionType::kBackupPasswordEntry
              : autofill::SuggestionType::kPasswordEntry;

      FormSuggestionMetadata metadata;
      metadata.is_single_username_form = is_single_username_form;
      metadata.likely_from_real_password_field = isPasswordField;

      [results
          addObject:[FormSuggestion suggestionWithValue:username
                                     displayDescription:realm
                                                   icon:nil
                                                   type:suggestionType
                                                payload:autofill::Suggestion::
                                                            Payload()
                                         requiresReauth:YES
                             acceptanceA11yAnnouncement:nil
                                               metadata:std::move(metadata)]];
    }
  }

  return [results copy];
}

// Adds a pending form extraction query and schedules a cleanup task in the case
// the query hangs.
- (void)addPendingQuery:(PendingFormQuery*)query {
  [_pendingFormQueries addObject:query];
  [self scheduleCleanupIfNeeded];
}

- (void)checkIfSuggestionsAvailableForForm:
            (FormSuggestionProviderQuery*)formQuery
                         completionHandler:
                             (SuggestionsAvailableCompletion)completion {
  // When password controller's -processWithPasswordFormFillData: is already
  // called, `completion` will be called immediately and `triggerFormExtraction`
  // will be skipped.
  // Otherwise, -suggestionHelperShouldTriggerFormExtraction: will be called
  // and `completion` will not be called until
  // -processWithPasswordFormFillData: is called.
  DCHECK(_webState.get());

  const std::string frame_id = SysNSStringToUTF8(formQuery.frameID);
  web::WebFrame* frame = [self frameWithId:frame_id];
  DCHECK(frame);

  BOOL isPasswordField = [self isPasswordFieldOnForm:formQuery webFrame:frame];
  PendingFormQuery* query =
      [[PendingFormQuery alloc] initWithQuery:formQuery
                                   completion:completion
                             fillDataProvider:self
                              isPasswordField:isPasswordField];

  AccountSelectFillData* fillData = [self fillDataForFrameId:frame_id].first;

  if (![formQuery hasFocusType] || !fillData->Empty() ||
      _framesFormExtractionStatus[frame_id] ==
          FormExtractionStatus::kCompleted) {
    // If the query isn't triggered by focusing on the form or there is fill
    // data available, complete the check immediately with the available fill
    // data. If there is fill data, it doesn't mean that there are suggestions
    // for the form targeted by the query, but at least there are some chances
    // that suggestions will be available. If the extraction status is complete,
    // it means we already know whether or not suggestions are available and
    // there's no point in attempting form extraction again, so we can run the
    // completion block right away and exit early.
    [query runCompletion];
    return;
  }

  // Queue the form query until the fill data is processed. The queue can handle
  // concurent calls to -checkIfSuggestionsAvailableForForm, which may happen
  // when there is more than one consumer of suggestions.
  [self addPendingQuery:query];

  // Try to extract password forms from the frame's renderer content
  // because there is no knowledge of any extraction done yet. If
  // -checkIfSuggestionsAvailableForForm is called before the first forms
  // are extracted, this may result in extracting the forms twice, which
  // is fine.
  //
  // It is important to always call -suggestionHelperShouldTriggerFormExtraction
  // when there is a new query queued to make sure that the pending query is
  // completed when processing the form extraction results. Leaving a query
  // uncompleted may result in the caller waiting forever for query results
  // (e.g. having the keyboard input accessory not showing any suggestion
  // because the pipeline is blocked by an hanging request).
  [self.delegate suggestionHelperShouldTriggerFormExtraction:self
                                                     inFrame:frame];
  _framesFormExtractionStatus[frame_id] = FormExtractionStatus::kRequested;
}

- (password_manager::FillDataRetrievalResult)
    passwordFillDataForUsername:(NSString*)username
        likelyRealPasswordField:(bool)passwordField
                 formIdentifier:(autofill::FormRendererId)formId
                fieldIdentifier:(autofill::FieldRendererId)fieldId
                        frameId:(const std::string&)frameId {
  auto [fill_data, is_new] = [self fillDataForFrameId:frameId];
  if (is_new) {
    // If the AccountSelectFillData was freshly created, there is no way there
    // is going to be FillData matching the query. Return an error as the result
    // with the exact reason why the FillData wasn't available. This would
    // ultimately lead not being to match the form for FillData but we wouldn't
    // know exactly why.
    return base::unexpected(
        password_manager::FillDataRetrievalStatus::kNoFrame);
  }
  return fill_data->GetFillData(SysNSStringToUTF16(username), formId, fieldId,
                                passwordField);
}

- (password_manager::FillDataRetrievalResult)
    passwordFillDataForUsername:(NSString*)username
                     forFrameId:(const std::string&)frameId {
  auto [fill_data, is_new] = [self fillDataForFrameId:frameId];
  if (is_new) {
    // If the AccountSelectFillData was freshly created, there is no way there
    // is going to be FillData matching the query. Return an error as the result
    // with the exact reason why the FillData wasn't available. This would
    // ultimately lead not being to match the form for FillData but we wouldn't
    // know exactly why.
    return base::unexpected(
        password_manager::FillDataRetrievalStatus::kNoFrame);
  }
  return fill_data->GetFillData(SysNSStringToUTF16(username));
}

- (void)resetForNewPage {
  _fillDataMap.clear();
  [_pendingFormQueries removeAllObjects];
  _framesFormExtractionStatus.clear();
}

- (void)processWithPasswordFormFillData:(const PasswordFormFillData&)formData
                             forFrameId:(const std::string&)frameId
                            isMainFrame:(BOOL)isMainFrame
                      forSecurityOrigin:(const url::Origin&)origin {
  DCHECK(_webState.get());
  [self fillDataForFrameId:frameId].first->Add(
      formData, [self shouldAlwaysPopulateRealmForFrame:frameId
                                            isMainFrame:isMainFrame
                                      forSecurityOrigin:origin]);

  // "attachListenersForBottomSheet" is used to add event listeners
  // to fields which must trigger a specific behavior. In this case,
  // the username and password fields' renderer ids are sent through
  // "attachListenersForBottomSheet" so that they may trigger the
  // password bottom sheet on focus events for these specific fields.
  std::vector<autofill::FieldRendererId> rendererIds(2);
  rendererIds[0] = formData.username_element_renderer_id;
  rendererIds[1] = formData.password_element_renderer_id;
  [self.delegate attachListenersForBottomSheet:rendererIds forFrameId:frameId];

  [self completePendingFormQueriesForFrameId:frameId];
}

- (void)processWithNoSavedCredentialsWithFrameId:(const std::string&)frameId {
  [self completePendingFormQueriesForFrameId:frameId];
}

- (BOOL)isPasswordFieldOnForm:(FormSuggestionProviderQuery*)formQuery
                     webFrame:(web::WebFrame*)webFrame {
  if (![formQuery.fieldType isEqualToString:kObfuscatedFieldType]) {
    return NO;
  }

  if (!_webState.get() || !webFrame) {
    // Return YES if the objects needed to prove that the field is a password
    // aren't available.
    return YES;
  }

  // If the new approach to detect password fields is enabled, first
  // check if the Password Manager thinks it is a password field, then fallback
  // to using Autofill if the Password Manager verdict is negative.
  return [self isPasswordFieldFromPasswordManagerPerspective:formQuery
                                                    webFrame:webFrame] ||
         [self isPasswordFieldFromAutofillPerspective:formQuery
                                             webFrame:webFrame];
}

// Returns YES if the field of the `formQuery` is in the Autofill form cache and
// has its type associated to a password type. Still returns YES if the type of
// the field couldn't be verified (e.g. when the field can't be found in the
// cache). Returns NO if the field type could be determined and its type isn't
// associated to a password.
- (BOOL)isPasswordFieldFromAutofillPerspective:
            (FormSuggestionProviderQuery*)formQuery
                                      webFrame:(web::WebFrame*)webFrame {
  auto* driver = autofill::AutofillDriverIOS::FromWebStateAndWebFrame(
      _webState.get(), webFrame);
  if (!driver) {
    return YES;
  }

  autofill::FormStructure* form_structure =
      driver->GetAutofillManager().FindCachedFormById(
          {driver->GetFrameToken(), formQuery.formRendererID});
  if (!form_structure) {
    return YES;
  }

  const auto& fields = form_structure->fields();
  auto itEnd = fields.end();
  auto it = std::find_if(fields.begin(), itEnd, [&](auto& field) {
    return formQuery.fieldRendererID == field->renderer_id();
  });
  if (it == itEnd) {
    return YES;
  }

  autofill::FieldType fieldType = (*it)->Type().GetStorableType();
  switch (GroupTypeOfFieldType(fieldType)) {
    case autofill::FieldTypeGroup::kPasswordField:
    case autofill::FieldTypeGroup::kNoGroup:
      return YES;  // May be a password field.
    case autofill::FieldTypeGroup::kName:
    case autofill::FieldTypeGroup::kEmail:
    case autofill::FieldTypeGroup::kCompany:
    case autofill::FieldTypeGroup::kAddress:
    case autofill::FieldTypeGroup::kPhone:
    case autofill::FieldTypeGroup::kCreditCard:
    case autofill::FieldTypeGroup::kTransaction:
    case autofill::FieldTypeGroup::kUsernameField:
    case autofill::FieldTypeGroup::kUnfillable:
    case autofill::FieldTypeGroup::kIban:
    case autofill::FieldTypeGroup::kStandaloneCvcField:
    case autofill::FieldTypeGroup::kAutofillAi:
    case autofill::FieldTypeGroup::kLoyaltyCard:
      return NO;
  }
}

#pragma mark - FillDataProvider

- (bool)
    areSuggestionsAvailableForFrameId:(NSString*)frameId
                       formRendererId:(autofill::FormRendererId)formRendererId
                      fieldRendererId:(autofill::FieldRendererId)fieldRendererId
                      isPasswordField:(bool)isPasswordField {
  return [self fillDataForFrameId:SysNSStringToUTF8(frameId)]
      .first->IsSuggestionsAvailable(formRendererId, fieldRendererId,
                                     isPasswordField);
}

#pragma mark - Private

- (web::WebFrame*)frameWithId:(const std::string&)frameId {
  password_manager::PasswordManagerJavaScriptFeature* feature =
      password_manager::PasswordManagerJavaScriptFeature::GetInstance();
  return feature->GetWebFramesManager(_webState.get())->GetFrameWithId(frameId);
}

// Returns whether to add the form's url as the Credential's realm if the realm
// is not specified.
- (bool)shouldAlwaysPopulateRealmForFrame:(const std::string&)frameId
                              isMainFrame:(BOOL)isMainFrame
                        forSecurityOrigin:(const url::Origin&)origin {
  CHECK(_webState.get());
  if (IsCrossOriginIframe(_webState.get(), isMainFrame, origin)) {
    return true;
  }

  web::WebFrame* frame = [self frameWithId:frameId];
  if (!frame) {
    return false;
  }

  auto* driver = autofill::AutofillDriverIOS::FromWebStateAndWebFrame(
      _webState.get(), frame);
  if (!driver) {
    return false;
  }
  return driver->GetAutofillClient().ShouldFormatForLargeKeyboardAccessory();
}

// Completes all the pending form queries that were queued for the frame that
// corresponds to `frameId`. The fill data may not be the freshest if there are
// still other outgoing forms extractions queries pending for the frame, but at
// least something will be provided and the queries completed (avoiding the
// query caller waiting indefinitely for a callback).
- (void)completePendingFormQueriesForFrameId:(const std::string&)frameId {
  NSMutableArray<PendingFormQuery*>* remainingQueries = [NSMutableArray array];
  for (PendingFormQuery* query in _pendingFormQueries) {
    if ([query.frameId isEqualToString:SysUTF8ToNSString(frameId)]) {
      [query runCompletion];
    } else {
      [remainingQueries addObject:query];
    }
  }
  _pendingFormQueries = remainingQueries;

  // Only if the form extraction request has been made from
  // PasswordSuggestionHelper do we set the extraction status' value to
  // completed. Otherwise, the request could have happened too early and not yet
  // contain the information we are interested in.
  if (_framesFormExtractionStatus[frameId] ==
      FormExtractionStatus::kRequested) {
    _framesFormExtractionStatus[frameId] = FormExtractionStatus::kCompleted;
  }
}

// Returns a pair where the first element is the AccountSelectFillData for the
// corresponding `frameId` and the second element a bool that is true if the
// AccountSelectFillData had to be lazily created (i.e. didn't exist before).
- (std::pair<AccountSelectFillData*, bool>)fillDataForFrameId:
    (const std::string&)frameId {
  // Create empty AccountSelectFillData for the frame if it doesn't exist.
  auto insert_result = _fillDataMap.insert(
      std::make_pair(frameId, std::make_unique<AccountSelectFillData>()));
  return std::make_pair(insert_result.first->second.get(),
                        insert_result.second);
}

// Completes, if needed, frame extraction for `frameId`.
- (void)completeFormExtractionForFrame:(const std::string&)frameId {
  if (const auto it = _framesFormExtractionStatus.find(frameId);
      it != _framesFormExtractionStatus.end() &&
      it->second == FormExtractionStatus::kRequested) {
    _framesFormExtractionStatus[frameId] = FormExtractionStatus::kCompleted;
  }
}

// Schedules a cleanup task to clean up the expired queries for which no
// response was ever received within the time limit. Only schedules the task if
// there isn't already a task scheduled and there are still pending form queries
// that are subject to hanging. Is no op if the feature isn't enabled.
- (void)scheduleCleanupIfNeeded {
  if (_cleanupScheduled || _pendingFormQueries.count == 0 ||
      !base::FeatureList::IsEnabled(
          password_manager::features::
              kIosCleanupHangingPasswordFormExtractionRequests)) {
    return;
  }

  _cleanupScheduled = YES;

  __weak __typeof(self) weakSelf = self;
  base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(
          [](__typeof(self) strongSelf) {
            [strongSelf completeExpiredQueries];
          },
          weakSelf),
      GetCleanupTaskPeriodMs());
}

// Completes all expired queries, across frames. Called by the scheduled cleanup
// task. Automatically reassesses if a follow-up cleanup task is required after
// this cleanup in the case there as still pending not yet expired queries.
- (void)completeExpiredQueries {
  _cleanupScheduled = NO;
  NSMutableArray<PendingFormQuery*>* remainingQueries = [NSMutableArray array];
  for (PendingFormQuery* query in _pendingFormQueries) {
    if ([query isExpired]) {
      [query runCompletion];
      // Complete the extraction for the frame targeted by this expired
      // `query`.
      std::string frameId = SysNSStringToUTF8(query.frameId);
      [self completeFormExtractionForFrame:frameId];
    } else {
      [remainingQueries addObject:query];
    }
  }
  _pendingFormQueries = remainingQueries;
  [self scheduleCleanupIfNeeded];
}

// Returns YES if Password Manager thinks the field in the `formQuery` is a
// password field or returns NO otherwise.
- (BOOL)isPasswordFieldFromPasswordManagerPerspective:
            (FormSuggestionProviderQuery*)formQuery
                                             webFrame:(web::WebFrame*)webFrame {
  FieldRendererId field_id = formQuery.fieldRendererID;

  if (!_passwordManager || !field_id) {
    return NO;
  }

  password_manager::PasswordManagerDriver* driver =
      IOSPasswordManagerDriverFactory::FromWebStateAndWebFrame(_webState.get(),
                                                               webFrame);
  // There must be a driver if there is a frame.
  CHECK(driver);

  const password_manager::PasswordForm* form =
      _passwordManager->GetPasswordFormCache()->GetPasswordForm(
          driver, formQuery.formRendererID);

  return form != nullptr &&
         (form->password_element_renderer_id == field_id ||
          form->new_password_element_renderer_id == field_id ||
          form->confirmation_password_element_renderer_id == field_id);
}

@end
