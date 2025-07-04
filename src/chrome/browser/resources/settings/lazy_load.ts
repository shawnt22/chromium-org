// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Subpages
import './ai_page/ai_tab_organization_subpage.js';
import './ai_page/history_search_page.js';
import './ai_page/offer_writing_help_page.js';
import './appearance_page/appearance_fonts_page.js';
import './autofill_page/autofill_ai_section.js';
import './autofill_page/autofill_section.js';
// <if expr="is_win or is_macosx">
import './autofill_page/passkeys_subpage.js';
// </if>
import './autofill_page/payments_section.js';
// <if expr="not is_chromeos">
import './clear_browsing_data_dialog/clear_browsing_data_account_indicator.js';
// </if>
import './clear_browsing_data_dialog/clear_browsing_data_dialog.js';
import './clear_browsing_data_dialog/clear_browsing_data_dialog_v2.js';
import './clear_browsing_data_dialog/clear_browsing_data_time_picker.js';
import './search_engines_page/search_engines_page.js';
import './simple_confirmation_dialog.js';
import './clear_browsing_data_dialog/other_google_data_dialog.js';
import './privacy_page/anti_abuse_page.js';
import './privacy_page/privacy_guide/privacy_guide_dialog.js';
import './privacy_page/privacy_guide/privacy_guide_page.js';
import './privacy_sandbox/privacy_sandbox_ad_measurement_subpage.js';
import './privacy_sandbox/privacy_sandbox_fledge_subpage.js';
import './privacy_sandbox/privacy_sandbox_interest_item.js';
import './privacy_sandbox/privacy_sandbox_page.js';
import './privacy_page/incognito_tracking_protections_page.js';
import './privacy_sandbox/privacy_sandbox_manage_topics_subpage.js';
import './privacy_sandbox/privacy_sandbox_topics_subpage.js';
import './privacy_page/security_keys_subpage.js';
import './privacy_page/security_page_v2.js';
import './privacy_page/security_page.js';
import './safety_hub/safety_hub_page.js';
import './safety_hub/safety_hub_entry_point.js';
import './site_settings/all_sites.js';
import './site_settings/file_system_site_details.js';
import './site_settings/file_system_site_entry.js';
import './site_settings/file_system_site_entry_item.js';
import './site_settings/file_system_site_list.js';
import './site_settings_page/site_settings_page.js';
import './site_settings/category_setting_exceptions.js';
import './site_settings/chooser_exception_list.js';
import './site_settings/site_details_permission_device_entry.js';
import './site_settings/geolocation_page.js';
import './site_settings/media_picker.js';
import './site_settings/notifications_page.js';
import './site_settings/pdf_documents.js';
import './site_settings/protocol_handlers.js';
import './site_settings/settings_category_default_radio_group.js';
import './site_settings/site_data.js';
import './site_settings/site_details.js';
import './site_settings/zoom_levels.js';
// <if expr="not is_chromeos">
import './a11y_page/live_caption_section.js';
import './people_page/import_data_dialog.js';
// </if>
// <if expr="not chromeos_ash">
import './people_page/manage_profile.js';
// </if>
import './people_page/signout_dialog.js';
import './people_page/sync_controls.js';
import './people_page/sync_page.js';
// Sections
import './a11y_page/a11y_page.js';
import './downloads_page/downloads_page.js';
// <if expr="not chromeos_ash">
import './languages_page/languages.js';
import './languages_page/languages_page.js';
import './languages_page/spell_check_page.js';
import './languages_page/translate_page.js';
// </if>
import './reset_page/reset_page.js';
// <if expr="not chromeos_ash">
import './system_page/system_page.js';
// </if>

// <if expr="not chromeos_ash and not is_macosx">
import './languages_page/edit_dictionary_page.js';

// </if>

// <if expr="not is_chromeos">
export {ScreenAiInstallStatus} from '/shared/settings/a11y_page/ax_annotations_browser_proxy.js';
export {CaptionsBrowserProxy, CaptionsBrowserProxyImpl, LiveCaptionLanguageList} from '/shared/settings/a11y_page/captions_browser_proxy.js';
// </if>

export {FontsBrowserProxy, FontsBrowserProxyImpl, FontsData} from '/shared/settings/appearance_page/fonts_browser_proxy.js';
export {CrShortcutInputElement} from 'chrome://resources/cr_components/cr_shortcut_input/cr_shortcut_input.js';
export {CrButtonElement} from 'chrome://resources/cr_elements/cr_button/cr_button.js';
export {CrCheckboxElement} from 'chrome://resources/cr_elements/cr_checkbox/cr_checkbox.js';
export {CrCollapseElement} from 'chrome://resources/cr_elements/cr_collapse/cr_collapse.js';
export {CrDialogElement} from 'chrome://resources/cr_elements/cr_dialog/cr_dialog.js';
export {CrExpandButtonElement} from 'chrome://resources/cr_elements/cr_expand_button/cr_expand_button.js';
export {CrIconButtonElement} from 'chrome://resources/cr_elements/cr_icon_button/cr_icon_button.js';
export {CrInputElement} from 'chrome://resources/cr_elements/cr_input/cr_input.js';
export {CrLazyRenderElement} from 'chrome://resources/cr_elements/cr_lazy_render/cr_lazy_render.js';
export {CrSliderElement} from 'chrome://resources/cr_elements/cr_slider/cr_slider.js';
export {CrTextareaElement} from 'chrome://resources/cr_elements/cr_textarea/cr_textarea.js';
export {getToastManager} from 'chrome://resources/cr_elements/cr_toast/cr_toast_manager.js';
export {CrTooltipElement} from 'chrome://resources/cr_elements/cr_tooltip/cr_tooltip.js';
export {IronListElement} from 'chrome://resources/polymer/v3_0/iron-list/iron-list.js';
export {AccessibilityBrowserProxy, AccessibilityBrowserProxyImpl} from './a11y_page/a11y_browser_proxy.js';
export {SettingsA11yPageElement} from './a11y_page/a11y_page.js';
// <if expr="is_win or is_linux or is_macosx">
export {SettingsAxAnnotationsSectionElement} from './a11y_page/ax_annotations_section.js';
// </if>
// <if expr="not is_chromeos">
export {SettingsLiveCaptionElement} from './a11y_page/live_caption_section.js';
export {SettingsLiveTranslateElement} from './a11y_page/live_translate_section.js';
// </if>

export {SettingsAiCompareSubpageElement} from './ai_page/ai_compare_subpage.js';
export {isFeatureDisabledByPolicy, SettingsAiPolicyIndicator} from './ai_page/ai_policy_indicator.js';
export {SettingsAiTabOrganizationSubpageElement} from './ai_page/ai_tab_organization_subpage.js';
export {AiEnterpriseFeaturePrefName, AiPageActions, FeatureOptInState, SettingsAiPageFeaturePrefName} from './ai_page/constants.js';
export {SettingsHistorySearchPageElement} from './ai_page/history_search_page.js';
export {COMPOSE_PROACTIVE_NUDGE_DISABLED_SITES_PREF, COMPOSE_PROACTIVE_NUDGE_PREF, SettingsOfferWritingHelpPageElement} from './ai_page/offer_writing_help_page.js';
export {SettingsAppearanceFontsPageElement} from './appearance_page/appearance_fonts_page.js';
export {SettingsAddressEditDialogElement} from './autofill_page/address_edit_dialog.js';
export {SettingsAddressRemoveConfirmationDialogElement} from './autofill_page/address_remove_confirmation_dialog.js';
export {SettingsAutofillAiAddOrEditDialogElement} from './autofill_page/autofill_ai_add_or_edit_dialog.js';
export {SettingsAutofillAiSectionElement} from './autofill_page/autofill_ai_section.js';
export {AutofillManagerImpl, AutofillManagerProxy, PersonalDataChangedListener} from './autofill_page/autofill_manager_proxy.js';
export {SettingsAutofillSectionElement} from './autofill_page/autofill_section.js';
export {CountryDetailManagerProxy, CountryDetailManagerProxyImpl} from './autofill_page/country_detail_manager_proxy.js';
export {SettingsCreditCardEditDialogElement} from './autofill_page/credit_card_edit_dialog.js';
export {SettingsCreditCardListEntryElement} from './autofill_page/credit_card_list_entry.js';
export {EntityDataManagerProxy, EntityDataManagerProxyImpl, EntityInstancesChangedListener} from './autofill_page/entity_data_manager_proxy.js';
export {SettingsIbanEditDialogElement} from './autofill_page/iban_edit_dialog.js';
export {SettingsIbanListEntryElement} from './autofill_page/iban_list_entry.js';
// <if expr="is_win or is_macosx">
export {Passkey, PasskeysBrowserProxy, PasskeysBrowserProxyImpl} from './autofill_page/passkeys_browser_proxy.js';
export {SettingsPasskeysSubpageElement} from './autofill_page/passkeys_subpage.js';
// </if>
export {SettingsPayOverTimeIssuerListEntryElement} from './autofill_page/pay_over_time_issuer_list_entry.js';
export {SettingsPaymentsListElement} from './autofill_page/payments_list.js';
export {PaymentsManagerImpl, PaymentsManagerProxy} from './autofill_page/payments_manager_proxy.js';
export {SettingsPaymentsSectionElement} from './autofill_page/payments_section.js';
export {SettingsVirtualCardUnenrollDialogElement} from './autofill_page/virtual_card_unenroll_dialog.js';
// <if expr="not is_chromeos">
export {SettingsClearBrowsingDataAccountIndicator} from './clear_browsing_data_dialog/clear_browsing_data_account_indicator.js';
// </if>
export {BrowsingDataType, ClearBrowsingDataBrowserProxy, ClearBrowsingDataBrowserProxyImpl, ClearBrowsingDataResult, TimePeriod, UpdateSyncStateEvent} from './clear_browsing_data_dialog/clear_browsing_data_browser_proxy.js';
export {SettingsClearBrowsingDataDialogElement} from './clear_browsing_data_dialog/clear_browsing_data_dialog.js';
export {getDataTypePrefName, SettingsClearBrowsingDataDialogV2Element} from './clear_browsing_data_dialog/clear_browsing_data_dialog_v2.js';
export {getTimePeriodString, SettingsClearBrowsingDataTimePicker} from './clear_browsing_data_dialog/clear_browsing_data_time_picker.js';
export {SettingsHistoryDeletionDialogElement} from './clear_browsing_data_dialog/history_deletion_dialog.js';
export {SettingsOtherGoogleDataDialogElement} from './clear_browsing_data_dialog/other_google_data_dialog.js';
export {SettingsPasswordsDeletionDialogElement} from './clear_browsing_data_dialog/passwords_deletion_dialog.js';
export {ControlledButtonElement} from './controls/controlled_button.js';
export {SettingsCheckboxElement} from './controls/settings_checkbox.js';
export {SettingsRadioGroupElement} from './controls/settings_radio_group.js';
export {SettingsSliderElement} from './controls/settings_slider.js';
export {SettingsToggleButtonElement} from './controls/settings_toggle_button.js';
export {DownloadsBrowserProxy, DownloadsBrowserProxyImpl} from './downloads_page/downloads_browser_proxy.js';
export {SettingsDownloadsPageElement} from './downloads_page/downloads_page.js';
// <if expr="not chromeos_ash">
export {SettingsAddLanguagesDialogElement} from './languages_page/add_languages_dialog.js';
export {getLanguageHelperInstance} from './languages_page/languages.js';
// <if expr="not is_macosx">
export {SettingsEditDictionaryPageElement} from './languages_page/edit_dictionary_page.js';
// </if>
export {LanguagesBrowserProxy, LanguagesBrowserProxyImpl} from './languages_page/languages_browser_proxy.js';
export {kMenuCloseDelay, SettingsLanguagesPageElement} from './languages_page/languages_page.js';
export {LanguageSettingsActionType, LanguageSettingsMetricsProxy, LanguageSettingsMetricsProxyImpl, LanguageSettingsPageImpressionType} from './languages_page/languages_settings_metrics_proxy.js';
export {LanguageHelper, LanguagesModel} from './languages_page/languages_types.js';
export {SettingsSpellCheckPageElement} from './languages_page/spell_check_page.js';
export {SettingsTranslatePageElement} from './languages_page/translate_page.js';
// </if>
// <if expr="not is_chromeos">
export {BrowserProfile, ImportDataBrowserProxy, ImportDataBrowserProxyImpl, ImportDataStatus} from './people_page/import_data_browser_proxy.js';
export {SettingsImportDataDialogElement} from './people_page/import_data_dialog.js';
// </if>
// <if expr="not chromeos_ash">
export {SettingsManageProfileElement} from './people_page/manage_profile.js';
export {ManageProfileBrowserProxy, ManageProfileBrowserProxyImpl, ProfileShortcutStatus} from './people_page/manage_profile_browser_proxy.js';
// </if>
export {SettingsSyncControlsElement} from './people_page/sync_controls.js';
export {SettingsSyncEncryptionOptionsElement} from './people_page/sync_encryption_options.js';
export {SettingsSyncPageElement} from './people_page/sync_page.js';
export {NetworkPredictionOptions} from './performance_page/constants.js';
export {SettingsAntiAbusePageElement} from './privacy_page/anti_abuse_page.js';
export {SettingsCollapseRadioButtonElement} from './privacy_page/collapse_radio_button.js';
export {SettingsCookiesPageElement} from './privacy_page/cookies_page.js';
export {CrLottieElement} from './privacy_page/cr_lottie.js';
export {SettingsDoNotTrackToggleElement} from './privacy_page/do_not_track_toggle.js';
export {FINGERPRINT_CHECK_DARK_URL, FINGERPRINT_CHECK_LIGHT_URL, FINGERPRINT_SCANNED_ICON_DARK, FINGERPRINT_SCANNED_ICON_LIGHT, FingerprintProgressArcElement, PROGRESS_CIRCLE_BACKGROUND_COLOR_DARK, PROGRESS_CIRCLE_BACKGROUND_COLOR_LIGHT, PROGRESS_CIRCLE_FILL_COLOR_DARK, PROGRESS_CIRCLE_FILL_COLOR_LIGHT} from './privacy_page/fingerprint_progress_arc.js';
export {IncognitoTrackingProtectionsPageElement} from './privacy_page/incognito_tracking_protections_page.js';
export {SettingsPersonalizationOptionsElement} from './privacy_page/personalization_options.js';
export {PrivacyGuideStep} from './privacy_page/privacy_guide/constants.js';
export {PrivacyGuideAdTopicsFragmentElement} from './privacy_page/privacy_guide/privacy_guide_ad_topics_fragment.js';
export {PrivacyGuideCompletionFragmentElement} from './privacy_page/privacy_guide/privacy_guide_completion_fragment.js';
export {PrivacyGuideCookiesFragmentElement} from './privacy_page/privacy_guide/privacy_guide_cookies_fragment.js';
export {SettingsPrivacyGuideDialogElement} from './privacy_page/privacy_guide/privacy_guide_dialog.js';
export {PrivacyGuideHistorySyncFragmentElement} from './privacy_page/privacy_guide/privacy_guide_history_sync_fragment.js';
export {PrivacyGuideMsbbFragmentElement} from './privacy_page/privacy_guide/privacy_guide_msbb_fragment.js';
export {SettingsPrivacyGuidePageElement} from './privacy_page/privacy_guide/privacy_guide_page.js';
export {PrivacyGuideSafeBrowsingFragmentElement} from './privacy_page/privacy_guide/privacy_guide_safe_browsing_fragment.js';
export {PrivacyGuideWelcomeFragmentElement} from './privacy_page/privacy_guide/privacy_guide_welcome_fragment.js';
export {SecureDnsResolverType, SettingsSecureDnsElement} from './privacy_page/secure_dns.js';
export {SecureDnsInputElement} from './privacy_page/secure_dns_input.js';
export {BioEnrollDialogPage, SettingsSecurityKeysBioEnrollDialogElement} from './privacy_page/security_keys_bio_enroll_dialog.js';
export {Ctap2Status, SampleStatus, SecurityKeysBioEnrollProxy, SecurityKeysBioEnrollProxyImpl, SecurityKeysCredentialBrowserProxy, SecurityKeysCredentialBrowserProxyImpl, SecurityKeysPinBrowserProxy, SecurityKeysPinBrowserProxyImpl, SecurityKeysResetBrowserProxy, SecurityKeysResetBrowserProxyImpl} from './privacy_page/security_keys_browser_proxy.js';
export {CredentialManagementDialogPage, SettingsSecurityKeysCredentialManagementDialogElement} from './privacy_page/security_keys_credential_management_dialog.js';
export {ResetDialogPage, SettingsSecurityKeysResetDialogElement} from './privacy_page/security_keys_reset_dialog.js';
export {SetPinDialogPage, SettingsSecurityKeysSetPinDialogElement} from './privacy_page/security_keys_set_pin_dialog.js';
export {SecurityKeysSubpageElement} from './privacy_page/security_keys_subpage.js';
export {HttpsFirstModeSetting, SafeBrowsingSetting, SettingsSecurityPageElement} from './privacy_page/security_page.js';
export {SecuritySettingsBundleSetting, SettingsSecurityPageV2Element} from './privacy_page/security_page_v2.js';
export {SettingsPrivacySandboxAdMeasurementSubpageElement} from './privacy_sandbox/privacy_sandbox_ad_measurement_subpage.js';
export {SettingsPrivacySandboxFledgeSubpageElement} from './privacy_sandbox/privacy_sandbox_fledge_subpage.js';
export {PrivacySandboxInterestItemElement} from './privacy_sandbox/privacy_sandbox_interest_item.js';
export {SettingsPrivacySandboxManageTopicsSubpageElement} from './privacy_sandbox/privacy_sandbox_manage_topics_subpage.js';
export {SettingsPrivacySandboxPageElement} from './privacy_sandbox/privacy_sandbox_page.js';
export {SettingsPrivacySandboxTopicsSubpageElement} from './privacy_sandbox/privacy_sandbox_topics_subpage.js';
export {SettingsResetPageElement} from './reset_page/reset_page.js';
export {SettingsResetProfileDialogElement} from './reset_page/reset_profile_dialog.js';
export {SettingsSafetyHubExtensionsModuleElement} from './safety_hub/extensions_module.js';
export {SettingsSafetyHubNotificationPermissionsModuleElement} from './safety_hub/notification_permissions_module.js';
export {CardInfo, CardState, EntryPointInfo, NotificationPermission, SafetyHubBrowserProxy, SafetyHubBrowserProxyImpl, SafetyHubEvent, UnusedSitePermissions} from './safety_hub/safety_hub_browser_proxy.js';
export {SettingsSafetyHubCardElement} from './safety_hub/safety_hub_card.js';
export {SettingsSafetyHubEntryPointElement} from './safety_hub/safety_hub_entry_point.js';
export {SettingsSafetyHubModuleElement, SiteInfo, SiteInfoWithTarget} from './safety_hub/safety_hub_module.js';
export {SettingsSafetyHubPageElement} from './safety_hub/safety_hub_page.js';
export {SettingsSafetyHubUnusedSitePermissionsModuleElement} from './safety_hub/unused_site_permissions_module.js';
export {SettingsOmniboxExtensionEntryElement} from './search_engines_page/omnibox_extension_entry.js';
export {SettingsSearchEngineEditDialogElement} from './search_engines_page/search_engine_edit_dialog.js';
export {SettingsSearchEngineEntryElement} from './search_engines_page/search_engine_entry.js';
export {SettingsSearchEnginesListElement} from './search_engines_page/search_engines_list.js';
export {SettingsSearchEnginesPageElement} from './search_engines_page/search_engines_page.js';
export {SettingsSimpleConfirmationDialogElement} from './simple_confirmation_dialog.js';
export {AddSiteDialogElement} from './site_settings/add_site_dialog.js';
export {AllSitesElement} from './site_settings/all_sites.js';
export {CategorySettingExceptionsElement} from './site_settings/category_setting_exceptions.js';
export {ChooserExceptionListElement} from './site_settings/chooser_exception_list.js';
export {ChooserExceptionListEntryElement} from './site_settings/chooser_exception_list_entry.js';
export {ChooserType, ContentSetting, ContentSettingsTypes, CookieControlsMode, CookiesExceptionType, SettingsState, SITE_EXCEPTION_WILDCARD, SiteSettingSource, SortMethod} from './site_settings/constants.js';
export {SettingsEditExceptionDialogElement} from './site_settings/edit_exception_dialog.js';
export {FileSystemSiteDetailsElement} from './site_settings/file_system_site_details.js';
export {FileSystemSiteEntryElement} from './site_settings/file_system_site_entry.js';
export {FileSystemSiteEntryItemElement} from './site_settings/file_system_site_entry_item.js';
export {FileSystemSiteListElement} from './site_settings/file_system_site_list.js';
export {GeolocationPageElement} from './site_settings/geolocation_page.js';
export {NotificationsPageElement} from './site_settings/notifications_page.js';
export {AppHandlerEntry, AppProtocolEntry, HandlerEntry, ProtocolEntry, ProtocolHandlersElement} from './site_settings/protocol_handlers.js';
export {SettingsCategoryDefaultRadioGroupElement} from './site_settings/settings_category_default_radio_group.js';
export {SettingsSiteDataElement} from './site_settings/site_data.js';
export {SiteDetailsElement} from './site_settings/site_details.js';
export {SiteDetailsPermissionElement} from './site_settings/site_details_permission.js';
export {SiteDetailsPermissionDeviceEntryElement} from './site_settings/site_details_permission_device_entry.js';
export {SiteEntryElement} from './site_settings/site_entry.js';
export {SiteListElement} from './site_settings/site_list.js';
export {SiteListEntryElement} from './site_settings/site_list_entry.js';
export {ChooserException, DefaultContentSetting, DefaultSettingSource, FileSystemGrant, OriginFileSystemGrants, OriginInfo, RawChooserException, RawSiteException, RecentSitePermissions, SiteException, SiteGroup, SiteSettingsPrefsBrowserProxy, SiteSettingsPrefsBrowserProxyImpl, StorageAccessEmbeddingException, StorageAccessSiteException, ThirdPartyCookieBlockingSetting, ZoomLevelEntry} from './site_settings/site_settings_prefs_browser_proxy.js';
export {SettingsSmartCardReadersPageElement} from './site_settings/smart_card_readers_page.js';
export {StorageAccessSiteListElement} from './site_settings/storage_access_site_list.js';
export {StorageAccessSiteListEntryElement} from './site_settings/storage_access_site_list_entry.js';
export {StorageAccessStaticSiteListEntry, StorageAccessStaticSiteListEntryElement} from './site_settings/storage_access_static_site_list_entry.js';
export {WebsiteUsageBrowserProxy, WebsiteUsageBrowserProxyImpl} from './site_settings/website_usage_browser_proxy.js';
export {ZoomLevelsElement} from './site_settings/zoom_levels.js';
export {SettingsRecentSitePermissionsElement} from './site_settings_page/recent_site_permissions.js';
export {defaultSettingLabel} from './site_settings_page/site_settings_list.js';
export {SettingsSiteSettingsPageElement} from './site_settings_page/site_settings_page.js';
// <if expr="not chromeos_ash">
export {SettingsSystemPageElement} from './system_page/system_page.js';
export {SystemPageBrowserProxy, SystemPageBrowserProxyImpl} from './system_page/system_page_browser_proxy.js';

// </if>
