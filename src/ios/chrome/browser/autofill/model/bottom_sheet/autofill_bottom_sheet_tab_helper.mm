// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "ios/chrome/browser/autofill/model/bottom_sheet/autofill_bottom_sheet_tab_helper.h"

#import <algorithm>

#import "base/containers/contains.h"
#import "base/feature_list.h"
#import "base/functional/bind.h"
#import "base/functional/callback_helpers.h"
#import "base/metrics/histogram_functions.h"
#import "base/strings/string_util.h"
#import "base/time/time.h"
#import "components/autofill/core/browser/data_manager/payments/payments_data_manager.h"
#import "components/autofill/core/browser/data_manager/personal_data_manager.h"
#import "components/autofill/core/browser/form_structure.h"
#import "components/autofill/core/browser/payments/card_unmask_challenge_option.h"
#import "components/autofill/core/browser/suggestions/payments/payments_suggestion_generator.h"
#import "components/autofill/core/browser/suggestions/suggestion_type.h"
#import "components/autofill/core/browser/ui/payments/card_unmask_authentication_selection_dialog_controller_impl.h"
#import "components/autofill/core/browser/ui/payments/virtual_card_enroll_ui_model.h"
#import "components/autofill/core/common/autofill_features.h"
#import "components/autofill/core/common/form_data.h"
#import "components/autofill/core/common/unique_ids.h"
#import "components/autofill/ios/browser/autofill_client_ios.h"
#import "components/autofill/ios/browser/autofill_driver_ios.h"
#import "components/autofill/ios/common/features.h"
#import "components/autofill/ios/form_util/form_activity_params.h"
#import "components/password_manager/core/browser/features/password_features.h"
#import "components/password_manager/core/common/password_manager_features.h"
#import "components/password_manager/ios/password_manager_java_script_feature.h"
#import "components/plus_addresses/plus_address_types.h"
#import "components/prefs/pref_service.h"
#import "ios/chrome/browser/autofill/model/bottom_sheet/autofill_bottom_sheet_java_script_feature.h"
#import "ios/chrome/browser/autofill/model/bottom_sheet/autofill_bottom_sheet_observer.h"
#import "ios/chrome/browser/autofill/model/features.h"
#import "ios/chrome/browser/autofill/model/form_input_suggestions_provider.h"
#import "ios/chrome/browser/autofill/model/form_suggestion_tab_helper.h"
#import "ios/chrome/browser/shared/model/prefs/pref_names.h"
#import "ios/chrome/browser/shared/model/profile/profile_ios.h"
#import "ios/chrome/browser/shared/public/commands/autofill_commands.h"
#import "ios/chrome/browser/shared/public/commands/command_dispatcher.h"
#import "ios/chrome/browser/shared/public/features/features.h"
#import "ios/web/public/js_messaging/script_message.h"
#import "ios/web/public/js_messaging/web_frame.h"
#import "ios/web/public/js_messaging/web_frames_manager.h"
#import "ios/web/public/navigation/navigation_context.h"

namespace {

// Whether the provided field type is one which can trigger the Payments Bottom
// Sheet.
bool IsPaymentsBottomSheetTriggeringField(autofill::FieldType type) {
  switch (type) {
    case autofill::CREDIT_CARD_NAME_FULL:
    case autofill::CREDIT_CARD_NUMBER:
    case autofill::CREDIT_CARD_EXP_MONTH:
    case autofill::CREDIT_CARD_EXP_2_DIGIT_YEAR:
    case autofill::CREDIT_CARD_EXP_4_DIGIT_YEAR:
    case autofill::CREDIT_CARD_EXP_DATE_2_DIGIT_YEAR:
    case autofill::CREDIT_CARD_EXP_DATE_4_DIGIT_YEAR:
      return true;
    default:
      return false;
  }
}

// Returns true if there is any credit card suggestion in the `suggestions`.
bool HasAnyCreditCardSuggestion(NSArray<FormSuggestion*>* suggestions) {
  for (FormSuggestion* suggestion in suggestions) {
    if (suggestion.type == autofill::SuggestionType::kCreditCardEntry ||
        suggestion.type == autofill::SuggestionType::kVirtualCreditCardEntry) {
      return true;
    }
  }
  return false;
}

// Records the histograms related to the outcome of triggering the
// Payments Bottom Sheet V3 (triggered or didn't trigger).
void RecordPaymentsBottomSheetTriggerOutcome(bool did_trigger,
                                             base::TimeDelta trigger_walltime) {
  if (did_trigger) {
    base::UmaHistogramTimes("IOS.PaymentsBottomSheet.TimeToTrigger.Triggered",
                            trigger_walltime);
  } else {
    base::UmaHistogramTimes(
        "IOS.PaymentsBottomSheet.TimeToTrigger.NotTriggered", trigger_walltime);
  }
  base::UmaHistogramBoolean("IOS.PaymentsBottomSheetV3.Triggered",
                            /*sample=*/did_trigger);
}

bool UseV3() {
  return base::FeatureList::IsEnabled(kStatelessFormSuggestionController) &&
         base::FeatureList::IsEnabled(kAutofillPaymentsSheetV3Ios);
}

}  // namespace

AutofillBottomSheetTabHelper::~AutofillBottomSheetTabHelper() = default;

AutofillBottomSheetTabHelper::AutofillBottomSheetTabHelper(
    web::WebState* web_state)
    : web_state_(web_state) {
  frames_manager_observation_.Observe(
      AutofillBottomSheetJavaScriptFeature::GetInstance()->GetWebFramesManager(
          web_state));
  web_state->AddObserver(this);
}

// Public methods

void AutofillBottomSheetTabHelper::ShowCardUnmaskAuthenticationSelection(
    std::unique_ptr<
        autofill::CardUnmaskAuthenticationSelectionDialogControllerImpl>
        model_controller) {
  card_unmask_authentication_selection_controller_ =
      std::move(model_controller);
  [commands_handler_ showCardUnmaskAuthentication];
}

void AutofillBottomSheetTabHelper::ShowPlusAddressesBottomSheet(
    plus_addresses::PlusAddressCallback callback) {
  pending_plus_address_callback_ = std::move(callback);
  [commands_handler_ showPlusAddressesBottomSheet];
}

void AutofillBottomSheetTabHelper::ShowSaveCardBottomSheet(
    std::unique_ptr<autofill::SaveCardBottomSheetModel> model) {
  save_card_bottom_sheet_model_ = std::move(model);
  [commands_handler_ showSaveCardBottomSheetOnOriginWebState:web_state_];
}

void AutofillBottomSheetTabHelper::ShowVirtualCardEnrollmentBottomSheet(
    std::unique_ptr<autofill::VirtualCardEnrollUiModel> model,
    autofill::VirtualCardEnrollmentCallbacks callbacks) {
  virtual_card_enrollment_callbacks_ = std::move(callbacks);
  [commands_handler_ showVirtualCardEnrollmentBottomSheet:std::move(model)
                                           originWebState:web_state_];
}

void AutofillBottomSheetTabHelper::ShowEditAddressBottomSheet() {
  [commands_handler_ showEditAddressBottomSheet];
}

void AutofillBottomSheetTabHelper::SetAutofillBottomSheetHandler(
    id<AutofillCommands> commands_handler) {
  if (!commands_handler) {
    // Means that the web state has been destroyed therefore dismiss the edit
    // address bottom sheet if it's shown.
    [commands_handler_ dismissEditAddressBottomSheet];
  }
  commands_handler_ = commands_handler;
}

void AutofillBottomSheetTabHelper::SetPasswordGenerationProvider(
    id<PasswordGenerationProvider> generation_provider) {
  generation_provider_ = generation_provider;
}

void AutofillBottomSheetTabHelper::AddObserver(
    autofill::AutofillBottomSheetObserver* observer) {
  observers_.AddObserver(observer);
}

void AutofillBottomSheetTabHelper::RemoveObserver(
    autofill::AutofillBottomSheetObserver* observer) {
  observers_.RemoveObserver(observer);
}

void AutofillBottomSheetTabHelper::OnFormMessageReceived(
    const web::ScriptMessage& message) {
  autofill::FormActivityParams params;
  if (!commands_handler_ ||
      !autofill::FormActivityParams::FromMessage(message, &params)) {
    return;
  }

  const autofill::FieldRendererId renderer_id = params.field_renderer_id;
  std::string& frame_id = params.frame_id;
  bool is_password_related =
      base::Contains(registered_password_renderer_ids_[frame_id], renderer_id);
  bool is_payments_related =
      base::Contains(registered_payments_renderer_ids_[frame_id], renderer_id);
  bool is_password_generation_related = base::Contains(
      registered_password_generation_renderer_ids_[frame_id], renderer_id);

  if (is_password_related) {
    ShowPasswordBottomSheet(params);
  } else if (is_payments_related) {
    MaybeShowPaymentsBottomSheet(params);
  } else if (is_password_generation_related) {
    ShowProactivePasswordGenerationBottomSheet(params);
  }
}

void AutofillBottomSheetTabHelper::ShowPasswordBottomSheet(
    const autofill::FormActivityParams& params) {
  // Attempt to show the password suggestions bottom sheet. There is no
  // guarantee that it will be actually shown.
  [commands_handler_ showPasswordBottomSheet:params];
  if (base::FeatureList::IsEnabled(
          password_manager::features::kIOSPasswordBottomSheetV2)) {
    // In V2, detach the listeners right now since they've filled their purpose
    // of attempting to trigger the bottom sheet upon focusing on the login
    // field, making the listeners inoperative from now on. This helps
    // preventing having rogue listeners preempting the login fields forever
    // because the bottom sheet isn't behaving as expected (e.g. the bottom
    // sheet remains invisible while still waiting on an interaction from the
    // user to detach the listeners). In short, detaching the listeners can't
    // rely on signals from the bottom sheet UI, so we detach right here. There
    // is another mechanism used in the bottom sheet view itself to prevent the
    // keyboard from popping up over the bottom sheet. Postpone refocus for
    // later once the bottom sheet is dismissed.
    DetachPasswordListenersForAllFrames(/*refocus=*/false);
  }
}

void AutofillBottomSheetTabHelper::MaybeShowPaymentsBottomSheet(
    autofill::FormActivityParams params) {
  if (!UseV3()) {
    // Use the status quo logic for triggering the payments bottom sheet if
    // V3 isn't enabled.
    ShowPaymentsBottomSheet(params);
    return;
  }

  // In V3, First try to retrieve credit card suggestions before considering
  // triggering the payments bottom sheet. Credit card suggestions are a good
  // proxy for knowing that the type of the field is "settled" since we known
  // that the PWM was tested for suggestions (including the server predictions)
  // before getting the credit card suggestions.

  if (!web_state_) {
    return;
  }
  FormSuggestionTabHelper* tabHelper =
      FormSuggestionTabHelper::FromWebState(web_state_);
  if (!tabHelper) {
    return;
  }
  id<FormInputSuggestionsProvider> provider =
      tabHelper->GetAccessoryViewProvider();

  // Force this bit to true as retrieving Autofill suggestions for a form
  // requires a user manual gesture and we consider the signal to trigger
  // the bottom sheet as a valid signal as a manual gesture.
  params.has_user_gesture = true;

  auto completion = base::CallbackToBlock(base::BindOnce(
      &AutofillBottomSheetTabHelper::
          OnSuggestionsRetrievedForPaymentsBottomSheet,
      weak_factory_.GetWeakPtr(), params, base::TimeTicks::Now()));
  [provider retrieveSuggestionsForForm:params
                              webState:web_state_
              accessoryViewUpdateBlock:completion];
}

void AutofillBottomSheetTabHelper::OnSuggestionsRetrievedForPaymentsBottomSheet(
    const autofill::FormActivityParams& params,
    base::TimeTicks start_timestamp,
    NSArray<FormSuggestion*>* suggestions,
    id<FormInputSuggestionsProvider> provider) {
  auto trigger_walltime = base::TimeTicks::Now() - start_timestamp;
  bool has_cc_suggestions = HasAnyCreditCardSuggestion(suggestions);
  RecordPaymentsBottomSheetTriggerOutcome(/*did_trigger=*/has_cc_suggestions,
                                          trigger_walltime);
  if (has_cc_suggestions) {
    ShowPaymentsBottomSheet(params);
  }
}

void AutofillBottomSheetTabHelper::ShowPaymentsBottomSheet(
    const autofill::FormActivityParams& params) {
  for (auto& observer : observers_) {
    observer.WillShowPaymentsBottomSheet(params);
  }
  [commands_handler_ showPaymentsBottomSheet:params];
  if (base::FeatureList::IsEnabled(kAutofillPaymentsSheetV2Ios)) {
    // In V2, detach the listeners right away to make sure they're always
    // cleaned up to avoid issues with rogue listeners, see the
    // documentation in ShowPasswordBottomSheet() for more details. Postpone
    // refocus for later once the bottom sheet is dismissed.
    DetachPaymentsListenersForAllFrames(/*refocus=*/false);
  }
}

void AutofillBottomSheetTabHelper::ShowProactivePasswordGenerationBottomSheet(
    const autofill::FormActivityParams& params) {
  if (!web_state_) {
    return;
  }

  // Detach the listeners right away to make sure they're always
  // cleaned up to avoid issues with rogue listeners, see the documentation in
  // ShowPasswordBottomSheet() for more details. Postpone refocus for
  // later once the bottom sheet is dismissed.
  DetachPasswordGenerationListenersForAllFrames();

  web::WebFrame* frame =
      password_manager::PasswordManagerJavaScriptFeature::GetInstance()
          ->GetWebFramesManager(web_state_)
          ->GetFrameWithId(params.frame_id);
  if (!frame) {
    return;
  }
  [generation_provider_
      triggerPasswordGenerationForFormId:params.form_renderer_id
                         fieldIdentifier:params.field_renderer_id
                                 inFrame:frame
                               proactive:YES];
}

void AutofillBottomSheetTabHelper::AttachPasswordListeners(
    const std::vector<autofill::FieldRendererId>& renderer_ids,
    const std::string& frame_id) {
  bool silenced = HasReachedPasswordSuggestionDismissLimit();

  base::UmaHistogramBoolean("IOS.PasswordBottomSheet.Activated",
                            /*sample=*/!silenced);

  if (silenced) {
    // Do not allow displaying the sheet if silenced.
    return;
  }

  // Whether to only trigger the bottom sheet on trusted events.
  bool allow_autofocus = base::FeatureList::IsEnabled(
      password_manager::features::kIOSPasswordBottomSheetAutofocus);

  AttachListeners(renderer_ids, registered_password_renderer_ids_[frame_id],
                  frame_id, allow_autofocus, /*only_new=*/true);
}

void AutofillBottomSheetTabHelper::AttachPasswordGenerationListeners(
    const std::vector<autofill::FieldRendererId>& renderer_ids,
    const std::string& frame_id) {
  // Verify that the proactive password generation bottom sheet feature is
  // enabled and that it hasn't been dismissed too many times.
  if (!base::FeatureList::IsEnabled(
          password_manager::features::
              kIOSProactivePasswordGenerationBottomSheet) ||
      HasReachedPasswordGenerationDismissLimit()) {
    return;
  }

  AttachListeners(renderer_ids,
                  registered_password_generation_renderer_ids_[frame_id],
                  frame_id, /*allow_autofocus=*/true, /*only_new=*/true);
}

void AutofillBottomSheetTabHelper::AttachListeners(
    const std::vector<autofill::FieldRendererId>& renderer_ids,
    std::set<autofill::FieldRendererId>& registered_renderer_ids,
    const std::string& frame_id,
    bool allow_autofocus,
    bool only_new) {
  if (!web_state_) {
    return;
  }

  web::WebFramesManager* webFramesManager =
      AutofillBottomSheetJavaScriptFeature::GetInstance()->GetWebFramesManager(
          web_state_);

  web::WebFrame* frame = webFramesManager->GetFrameWithId(frame_id);
  if (!frame) {
    return;
  }

  // Transfer the renderer IDs to a set so that they are sorted and unique.
  std::set<autofill::FieldRendererId> sorted_renderer_ids(renderer_ids.begin(),
                                                          renderer_ids.end());
  // Get vector of new renderer IDs which aren't already registered.
  std::vector<autofill::FieldRendererId> new_renderer_ids;
  std::ranges::set_difference(sorted_renderer_ids, registered_renderer_ids,
                              std::back_inserter(new_renderer_ids));

  if (!new_renderer_ids.empty()) {
    // Add new renderer IDs to the list of registered renderer IDs.
    std::copy(
        new_renderer_ids.begin(), new_renderer_ids.end(),
        std::inserter(registered_renderer_ids, registered_renderer_ids.end()));
  }

  // Only attach the new renderer ids if `only_new` is true, attach all the
  // `renderer_ids` passed to AttachListeners() otherwise. The renderer will
  // end up just attaching the listeners to the elements that do not have a
  // listener yet, which includes the elements that had a listener in the past
  // but that were detached, so these elements will have a listener attached
  // again.
  auto& rendered_ids_to_attach = only_new ? new_renderer_ids : renderer_ids;

  if (!rendered_ids_to_attach.empty()) {
    // Enable the bottom sheet on the selected renderer ids.
    AutofillBottomSheetJavaScriptFeature::GetInstance()->AttachListeners(
        rendered_ids_to_attach, frame, allow_autofocus);
  }
}

void AutofillBottomSheetTabHelper::DetachPasswordListeners(
    const std::string& frame_id,
    bool refocus) {
  if (!web_state_) {
    return;
  }

  web::WebFramesManager* webFramesManager =
      AutofillBottomSheetJavaScriptFeature::GetInstance()->GetWebFramesManager(
          web_state_);
  web::WebFrame* frame = webFramesManager->GetFrameWithId(frame_id);

  AutofillBottomSheetJavaScriptFeature::GetInstance()->DetachListeners(
      registered_password_renderer_ids_[frame_id], frame, refocus);
}

void AutofillBottomSheetTabHelper::DetachPasswordListenersForAllFrames(
    bool refocus) {
  for (auto& registered_renderer_ids : registered_password_renderer_ids_) {
    DetachListenersForFrame(registered_renderer_ids.first,
                            registered_renderer_ids.second, refocus);
  }
}

void AutofillBottomSheetTabHelper::
    DetachPasswordGenerationListenersForAllFrames() {
  // Verify that the password generation bottom sheet feature is enabled.
  if (!base::FeatureList::IsEnabled(
          password_manager::features::
              kIOSProactivePasswordGenerationBottomSheet)) {
    return;
  }

  for (auto& registered_renderer_ids :
       registered_password_generation_renderer_ids_) {
    DetachListenersForFrame(registered_renderer_ids.first,
                            registered_renderer_ids.second, /*refocus=*/false);
  }
}

void AutofillBottomSheetTabHelper::DetachPaymentsListeners(
    const std::string& frame_id,
    bool refocus) {
  if (!web_state_) {
    return;
  }

  web::WebFramesManager* webFramesManager =
      AutofillBottomSheetJavaScriptFeature::GetInstance()->GetWebFramesManager(
          web_state_);
  web::WebFrame* frame = webFramesManager->GetFrameWithId(frame_id);

  AutofillBottomSheetJavaScriptFeature::GetInstance()->DetachListeners(
      registered_payments_renderer_ids_[frame_id], frame, refocus);
}

void AutofillBottomSheetTabHelper::DetachPaymentsListenersForAllFrames(
    bool refocus) {
  for (auto& registered_renderer_ids : registered_payments_renderer_ids_) {
    DetachListenersForFrame(registered_renderer_ids.first,
                            registered_renderer_ids.second, refocus);
  }
}

void AutofillBottomSheetTabHelper::DetachListenersForFrame(
    const std::string& frame_id,
    const std::set<autofill::FieldRendererId>& renderer_ids,
    bool refocus) {
  if (!web_state_) {
    return;
  }

  web::WebFramesManager* webFramesManager =
      AutofillBottomSheetJavaScriptFeature::GetInstance()->GetWebFramesManager(
          web_state_);
  web::WebFrame* frame = webFramesManager->GetFrameWithId(frame_id);
  AutofillBottomSheetJavaScriptFeature::GetInstance()->DetachListeners(
      renderer_ids, frame, refocus);
}

void AutofillBottomSheetTabHelper::RefocusElementIfNeeded(
    const std::string& frame_id) {
  if (!web_state_) {
    return;
  }

  web::WebFramesManager* webFramesManager =
      AutofillBottomSheetJavaScriptFeature::GetInstance()->GetWebFramesManager(
          web_state_);
  web::WebFrame* frame = webFramesManager->GetFrameWithId(frame_id);
  if (!frame) {
    return;
  }

  AutofillBottomSheetJavaScriptFeature::GetInstance()->RefocusElementIfNeeded(
      frame);
}

// WebStateObserver

void AutofillBottomSheetTabHelper::DidFinishNavigation(
    web::WebState* web_state,
    web::NavigationContext* navigation_context) {
  if (navigation_context->IsSameDocument()) {
    return;
  }

  // Clear all registered renderer ids
  registered_password_renderer_ids_.clear();
  registered_payments_renderer_ids_.clear();
}

void AutofillBottomSheetTabHelper::WebStateDestroyed(web::WebState* web_state) {
  web_state->RemoveObserver(this);
  frames_manager_observation_.Reset();
}

// web::WebFramesManager::Observer:
void AutofillBottomSheetTabHelper::WebFrameBecameAvailable(
    web::WebFramesManager* web_frames_manager,
    web::WebFrame* web_frame) {
  auto* driver = autofill::AutofillDriverIOS::FromWebStateAndWebFrame(
      web_state_, web_frame);
  if (!driver) {
    return;
  }
  autofill_manager_observations_.AddObservation(&driver->GetAutofillManager());
}

// autofill::AutofillManager::Observer

void AutofillBottomSheetTabHelper::OnAutofillManagerStateChanged(
    autofill::AutofillManager& manager,
    autofill::AutofillManager::LifecycleState old_state,
    autofill::AutofillManager::LifecycleState new_state) {
  using enum autofill::AutofillManager::LifecycleState;
  switch (new_state) {
    case kInactive:
    case kActive:
    case kPendingReset:
      break;
    case kPendingDeletion:
      autofill_manager_observations_.RemoveObservation(&manager);
      break;
  }
}

void AutofillBottomSheetTabHelper::AttachListenersForPaymentsForm(
    autofill::AutofillManager& manager,
    autofill::FormGlobalId form_id,
    bool only_new) {
  autofill::FormStructure* form_structure = manager.FindCachedFormById(form_id);
  if (!form_structure ||
      !form_structure->IsCompleteCreditCardForm(
          autofill::FormStructure::CreditCardFormCompleteness::
              kCompleteCreditCardForm)) {
    return;
  }
  if (autofill::GetCreditCardsToSuggest(
          manager.client().GetPersonalDataManager().payments_data_manager())
          .empty()) {
    return;
  }

  if (base::FeatureList::IsEnabled(
          autofill::features::kAutofillAcrossIframesIos)) {
    const autofill::AutofillDriverRouter& router =
        static_cast<autofill::AutofillClientIOS&>(manager.client())
            .GetAutofillDriverFactory()
            .router();
    // Break the browser form into renderer forms when the fix is enabled. Use
    // the entire browser form as the renderer form otherwise.
    auto renderer_forms = router.GetRendererForms(form_structure->ToFormData());

    for (const auto& renderer_form : renderer_forms) {
      std::vector<autofill::FieldRendererId> renderer_ids;
      for (const auto& field : renderer_form.fields()) {
        if (IsPaymentsBottomSheetTriggeringField(
                form_structure->GetFieldById(field.global_id())
                    ->Type()
                    .GetStorableType())) {
          renderer_ids.push_back(field.renderer_id());
        }
      }

      if (renderer_ids.empty()) {
        continue;
      }

      std::string renderer_form_frame_id =
          base::ToLowerASCII(renderer_form.host_frame()->ToString());

      // TODO(crbug.com/40266699): Remove `frame` once `renderer_ids` are
      // FieldGlobalIds.

      AttachListeners(renderer_ids,
                      registered_payments_renderer_ids_[renderer_form_frame_id],
                      renderer_form_frame_id, /*allow_autofocus=*/false,
                      only_new);
    }
  } else {
    std::vector<autofill::FieldRendererId> renderer_ids;
    for (const auto& field : form_structure->fields()) {
      if (IsPaymentsBottomSheetTriggeringField(
              field->Type().GetStorableType())) {
        renderer_ids.push_back(field->renderer_id());
      }
    }
    if (renderer_ids.empty()) {
      return;
    }
    // TODO(crbug.com/40266699): Remove `frame` once `renderer_ids` are
    // FieldGlobalIds.
    web::WebFrame* frame =
        static_cast<autofill::AutofillDriverIOS&>(manager.driver()).web_frame();
    if (!frame) {
      return;
    }
    std::string frame_id = frame->GetFrameId();
    AttachListeners(renderer_ids, registered_payments_renderer_ids_[frame_id],
                    frame_id, /*allow_autofocus=*/false, only_new);
  }
}

void AutofillBottomSheetTabHelper::OnFieldTypesDetermined(
    autofill::AutofillManager& manager,
    autofill::FormGlobalId form_id,
    FieldTypeSource source) {
  AttachListenersForPaymentsForm(manager, form_id, /*only_new=*/true);
}

std::unique_ptr<autofill::CardUnmaskAuthenticationSelectionDialogControllerImpl>
AutofillBottomSheetTabHelper::
    GetCardUnmaskAuthenticationSelectionDialogController() {
  return std::move(card_unmask_authentication_selection_controller_);
}

plus_addresses::PlusAddressCallback
AutofillBottomSheetTabHelper::GetPendingPlusAddressFillCallback() {
  return std::move(pending_plus_address_callback_);
}

std::unique_ptr<autofill::SaveCardBottomSheetModel>
AutofillBottomSheetTabHelper::GetSaveCardBottomSheetModel() {
  return std::move(save_card_bottom_sheet_model_);
}

autofill::VirtualCardEnrollmentCallbacks
AutofillBottomSheetTabHelper::GetVirtualCardEnrollmentCallbacks() {
  return std::move(virtual_card_enrollment_callbacks_);
}

// Private methods

bool AutofillBottomSheetTabHelper::HasReachedPasswordSuggestionDismissLimit() {
  const PrefService* pref_service =
      ProfileIOS::FromBrowserState(web_state_->GetBrowserState())->GetPrefs();
  bool dismissLimitReached =
      pref_service->GetInteger(prefs::kIosPasswordBottomSheetDismissCount) >=
      kPasswordBottomSheetMaxDismissCount;
  base::UmaHistogramBoolean("IOS.IsEnabled.Password.BottomSheet",
                            !dismissLimitReached);
  return dismissLimitReached;
}

bool AutofillBottomSheetTabHelper::HasReachedPasswordGenerationDismissLimit() {
  const PrefService* pref_service =
      ProfileIOS::FromBrowserState(web_state_->GetBrowserState())->GetPrefs();
  return pref_service->GetInteger(
             prefs::kIosPasswordGenerationBottomSheetDismissCount) >=
         kPasswordGenerationBottomSheetMaxDismissCount;
}
