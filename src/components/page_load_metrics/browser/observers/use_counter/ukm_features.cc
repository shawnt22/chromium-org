// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/no_destructor.h"
#include "components/page_load_metrics/browser/observers/use_counter_page_load_metrics_observer.h"

// This file defines a list of UseCounter WebFeature measured in the
// UKM-based UseCounter. Features must all satisfy UKM privacy requirements
// (see go/ukm). In addition, features should only be added if it's shown to be
// (or highly likely to be) rare, e.g. <1% of page views as measured by UMA.
//
// UKM-based UseCounter should be used to cover the case when UMA UseCounter
// data shows a behaviour that is rare but too common to blindly change.
// UKM-based UseCounter would allow us to find specific pages to reason about
// whether a breaking change is acceptable or not.
//
// This event is emitted for every page load and is not sub-sampled by the UKM
// system. The WebFeature::kPageVisits is always present in the event, so
// it is valid to take the ratio of events with your feature to kPageVisit to
// estimate "percentage of page views using a feature". Note that the lack of
// sub-sampling is the reason why this event must only be used for rare
// features.
//
// Obsolete UseCounters (kOBSOLETE_*) should be removed from this list.

using WebFeature = blink::mojom::WebFeature;

// UKM-based UseCounter features (WebFeature) should be defined in
// opt_in_features list.
const UseCounterMetricsRecorder::UkmFeatureList&
UseCounterMetricsRecorder::GetAllowedUkmFeatures() {
  static base::NoDestructor<UseCounterMetricsRecorder::UkmFeatureList>
      // We explicitly use an std::initializer_list below to work around GCC
      // bug 84849, which causes having a base::NoDestructor<T<U>> and passing
      // an initializer list of Us does not work.
      opt_in_features(std::initializer_list<WebFeature>({
          WebFeature::kNavigatorVibrate,
          WebFeature::kNavigatorVibrateSubFrame,
          WebFeature::kTouchEventPreventedNoTouchAction,
          WebFeature::kTouchEventPreventedForcedDocumentPassiveNoTouchAction,
          WebFeature::kMixedContentAudio,
          WebFeature::kMixedContentImage,
          WebFeature::kMixedContentVideo,
          WebFeature::kMixedContentPlugin,
          WebFeature::kOpenerNavigationWithoutGesture,
          WebFeature::kUsbRequestDevice,
          WebFeature::kXMLHttpRequestSynchronousInMainFrame,
          WebFeature::kXMLHttpRequestSynchronousInCrossOriginSubframe,
          WebFeature::kXMLHttpRequestSynchronousInSameOriginSubframe,
          WebFeature::kXMLHttpRequestSynchronousInWorker,
          WebFeature::kPaymentHandler,
          WebFeature::kPaymentRequestShowWithoutGesture,
          WebFeature::kPaymentRequestShowWithoutGestureOrToken,
          WebFeature::kCredentialManagerCreatePublicKeyCredential,
          WebFeature::kCredentialManagerGetPublicKeyCredential,
          WebFeature::kCredentialManagerMakePublicKeyCredentialSuccess,
          WebFeature::kCredentialManagerGetPublicKeyCredentialSuccess,
          WebFeature::kTextToSpeech_Speak,
          WebFeature::kTextToSpeech_SpeakDisallowedByAutoplay,
          WebFeature::kCSSEnvironmentVariable_SafeAreaInsetTop,
          WebFeature::kCSSEnvironmentVariable_SafeAreaInsetLeft,
          WebFeature::kCSSEnvironmentVariable_SafeAreaInsetRight,
          WebFeature::kCSSEnvironmentVariable_SafeAreaInsetBottom,
          WebFeature::kMediaControlsDisplayCutoutGesture,
          WebFeature::kPolymerV1Detected,
          WebFeature::kPolymerV2Detected,
          WebFeature::kFullscreenSecureOrigin,
          WebFeature::kFullscreenInsecureOrigin,
          WebFeature::kPrefixedVideoEnterFullscreen,
          WebFeature::kPrefixedVideoExitFullscreen,
          WebFeature::kPrefixedVideoEnterFullScreen,
          WebFeature::kPrefixedVideoExitFullScreen,
          WebFeature::kDocumentLevelPassiveDefaultEventListenerPreventedWheel,
          WebFeature::kDocumentDomainBlockedCrossOriginAccess,
          WebFeature::kDocumentDomainEnabledCrossOriginAccess,
          WebFeature::kCursorImageGT32x32,
          WebFeature::kCursorImageLE32x32,
          WebFeature::kCursorImageGT64x64,
          WebFeature::kAdClick,
          WebFeature::kUpdateWithoutShippingOptionOnShippingAddressChange,
          WebFeature::kUpdateWithoutShippingOptionOnShippingOptionChange,
          WebFeature::kSignedExchangeInnerResponseInMainFrame,
          WebFeature::kSignedExchangeInnerResponseInSubFrame,
          WebFeature::kWebShareShare,
          WebFeature::kDownloadInAdFrameWithoutUserGesture,
          WebFeature::kStorageBucketsOpen,
          WebFeature::kOpenWebDatabase,
          WebFeature::kV8MediaCapabilities_DecodingInfo_Method,
          WebFeature::kOpenerNavigationDownloadCrossOrigin,
          WebFeature::kLinkRelPrerender,
          WebFeature::kV8HTMLVideoElement_RequestPictureInPicture_Method,
          WebFeature::kMediaCapabilitiesDecodingInfoWithKeySystemConfig,
          WebFeature::kTextFragmentAnchor,
          WebFeature::kTextFragmentAnchorMatchFound,
          WebFeature::kCookieInsecureAndSameSiteNone,
          WebFeature::kCookieStoreAPI,
          WebFeature::kDeviceOrientationSecureOrigin,
          WebFeature::kDeviceOrientationAbsoluteSecureOrigin,
          WebFeature::kDeviceMotionSecureOrigin,
          WebFeature::kRelativeOrientationSensorConstructor,
          WebFeature::kAbsoluteOrientationSensorConstructor,
          WebFeature::kLinearAccelerationSensorConstructor,
          WebFeature::kAccelerometerConstructor,
          WebFeature::kGyroscopeConstructor,
          WebFeature::kServiceWorkerInterceptedRequestFromOriginDirtyStyleSheet,
          WebFeature::kDownloadPrePolicyCheck,
          WebFeature::kDownloadPostPolicyCheck,
          WebFeature::kDownloadInAdFrame,
          WebFeature::kDownloadInSandbox,
          WebFeature::kDownloadWithoutUserGesture,
          WebFeature::kLazyLoadFrameLoadingAttributeLazy,
          WebFeature::kLazyLoadFrameLoadingAttributeEager,
          WebFeature::kLazyLoadImageLoadingAttributeLazy,
          WebFeature::kLazyLoadImageLoadingAttributeEager,
          WebFeature::kDOMSubtreeModifiedEvent,
          WebFeature::kDOMNodeInsertedEvent,
          WebFeature::kDOMNodeRemovedEvent,
          WebFeature::kDOMNodeRemovedFromDocumentEvent,
          WebFeature::kDOMNodeInsertedIntoDocumentEvent,
          WebFeature::kDOMCharacterDataModifiedEvent,
          WebFeature::kWebOTP,
          WebFeature::kBaseWithCrossOriginHref,
          WebFeature::kWakeLockAcquireScreenLock,
          WebFeature::kWakeLockAcquireSystemLock,
          WebFeature::kThirdPartyServiceWorker,
          WebFeature::kThirdPartySharedWorker,
          WebFeature::kThirdPartyBroadcastChannel,
          WebFeature::kHeavyAdIntervention,
          WebFeature::kGetGamepadsFromCrossOriginSubframe,
          WebFeature::kGetGamepadsFromInsecureContext,
          WebFeature::kGetGamepads,
          WebFeature::kMovedOrResizedPopup,
          WebFeature::kMovedOrResizedPopup2sAfterCreation,
          WebFeature::kDOMWindowOpenPositioningFeatures,
          WebFeature::kCSSSelectorInternalMediaControlsOverlayCastButton,
          WebFeature::kWebBluetoothRequestDevice,
          WebFeature::kWebBluetoothRequestScan,
          WebFeature::
              kV8VideoPlaybackQuality_CorruptedVideoFrames_AttributeGetter,
          WebFeature::kV8MediaSession_Metadata_AttributeSetter,
          WebFeature::kV8MediaSession_SetActionHandler_Method,
          WebFeature::kLargeStickyAd,
          WebFeature::
              kElementWithLeftwardOrUpwardOverflowDirection_ScrollLeftOrTopSetPositive,
          WebFeature::kThirdPartyFileSystem,
          WebFeature::kThirdPartyIndexedDb,
          WebFeature::kThirdPartyCacheStorage,
          WebFeature::kDeclarativeShadowRoot,
          WebFeature::kOverlayPopup,
          WebFeature::kOverlayPopupAd,
          WebFeature::kTrustTokenXhr,
          WebFeature::kTrustTokenFetch,
          WebFeature::kTrustTokenIframe,
          WebFeature::kV8Document_HasPrivateToken_Method,
          WebFeature::kV8HTMLVideoElement_RequestVideoFrameCallback_Method,
          WebFeature::kV8HTMLVideoElement_CancelVideoFrameCallback_Method,
          WebFeature::kSchemefulSameSiteContextDowngrade,
          WebFeature::kIdleDetectionStart,
          WebFeature::kPerformanceObserverEntryTypesAndBuffered,
          WebFeature::kStorageAccessAPI_HasStorageAccess_Method,
          WebFeature::kStorageAccessAPI_requestStorageAccess_Method,
          WebFeature::kThirdPartyCookieRead,
          WebFeature::kThirdPartyCookieWrite,
          WebFeature::kCrossSitePostMessage,
          WebFeature::kSchemelesslySameSitePostMessage,
          WebFeature::kSchemelesslySameSitePostMessageSecureToInsecure,
          WebFeature::kSchemelesslySameSitePostMessageInsecureToSecure,
          WebFeature::kAddressSpaceLocalSecureContextEmbeddedLoopbackV2,
          WebFeature::kAddressSpaceLocalNonSecureContextEmbeddedLoopbackV2,
          WebFeature::kAddressSpacePublicSecureContextEmbeddedLoopbackV2,
          WebFeature::kAddressSpacePublicNonSecureContextEmbeddedLoopbackV2,
          WebFeature::kAddressSpacePublicSecureContextEmbeddedLocalV2,
          WebFeature::kAddressSpacePublicNonSecureContextEmbeddedLocalV2,
          WebFeature::kAddressSpaceUnknownSecureContextEmbeddedLoopbackV2,
          WebFeature::kAddressSpaceUnknownNonSecureContextEmbeddedLoopbackV2,
          WebFeature::kAddressSpaceUnknownSecureContextEmbeddedLocalV2,
          WebFeature::kAddressSpaceUnknownNonSecureContextEmbeddedLocalV2,
          WebFeature::kAddressSpaceLocalSecureContextNavigatedToLoopbackV2,
          WebFeature::kAddressSpaceLocalNonSecureContextNavigatedToLoopbackV2,
          WebFeature::kAddressSpacePublicSecureContextNavigatedToLoopbackV2,
          WebFeature::kAddressSpacePublicNonSecureContextNavigatedToLoopbackV2,
          WebFeature::kAddressSpacePublicSecureContextNavigatedToLocalV2,
          WebFeature::kAddressSpacePublicNonSecureContextNavigatedToLocalV2,
          WebFeature::kAddressSpaceUnknownSecureContextNavigatedToLoopbackV2,
          WebFeature::kAddressSpaceUnknownNonSecureContextNavigatedToLoopbackV2,
          WebFeature::kAddressSpaceUnknownSecureContextNavigatedToLocalV2,
          WebFeature::kAddressSpaceUnknownNonSecureContextNavigatedToLocalV2,
          WebFeature::kFileSystemPickerMethod,
          WebFeature::kV8Window_ShowOpenFilePicker_Method,
          WebFeature::kV8Window_ShowSaveFilePicker_Method,
          WebFeature::kV8Window_ShowDirectoryPicker_Method,
          WebFeature::kV8StorageManager_GetDirectory_Method,
          WebFeature::kBarcodeDetectorDetect,
          WebFeature::kFaceDetectorDetect,
          WebFeature::kTextDetectorDetect,
          WebFeature::kV8SharedArrayBufferConstructedWithoutIsolation,
          WebFeature::kV8HTMLVideoElement_GetVideoPlaybackQuality_Method,
          WebFeature::kWebPImage,
          WebFeature::kAVIFImage,
          WebFeature::kGetDisplayMedia,
          WebFeature::kLaxAllowingUnsafeCookies,
          WebFeature::kOversrollBehaviorOnViewportBreaks,
          WebFeature::kPaymentRequestCSPViolation,
          WebFeature::kRequestedFileSystemPersistentThirdPartyContext,
          WebFeature::kPrefixedStorageInfoThirdPartyContext,
          WebFeature::
              kCrossBrowsingContextGroupMainFrameNulledNonEmptyNameAccessed,
          WebFeature::kControlledWorkerWillBeUncontrolled,
          WebFeature::kUsbDeviceOpen,
          WebFeature::kWebBluetoothRemoteServerConnect,
          WebFeature::kSerialRequestPort,
          WebFeature::kSerialPortOpen,
          WebFeature::kHidRequestDevice,
          WebFeature::kHidDeviceOpen,
          WebFeature::kControlledNonBlobURLWorkerWillBeUncontrolled,
          WebFeature::kSameSiteCookieInclusionChangedByCrossSiteRedirect,
          WebFeature::
              kBlobStoreAccessAcrossAgentClustersInResolveAsURLLoaderFactory,
          WebFeature::kBlobStoreAccessAcrossAgentClustersInResolveForNavigation,
          WebFeature::kSearchEventFired,
          WebFeature::kReadOrWriteWebDatabase,
          WebFeature::kExternalProtocolBlockedBySandbox,
          WebFeature::kWebCodecsAudioDecoder,
          WebFeature::kWebCodecsVideoDecoder,
          WebFeature::kWebCodecsVideoEncoder,
          WebFeature::kWebCodecsVideoTrackReader,
          WebFeature::kWebCodecsImageDecoder,
          WebFeature::kWebCodecsAudioEncoder,
          WebFeature::kWebCodecsVideoFrameFromImage,
          WebFeature::kWebCodecsVideoFrameFromBuffer,
          WebFeature::kPrivateNetworkAccessPreflightWarning,
          WebFeature::kPrivateNetworkAccessPermissionPrompt,
          WebFeature::kWebBluetoothGetAvailability,
          WebFeature::kCookieHasNotBeenRefreshedIn201To300Days,
          WebFeature::kCookieHasNotBeenRefreshedIn301To350Days,
          WebFeature::kCookieHasNotBeenRefreshedIn351To400Days,
          WebFeature::kPartitionedCookies,
          WebFeature::kScriptSchedulingType_Defer,
          WebFeature::kScriptSchedulingType_ParserBlocking,
          WebFeature::kScriptSchedulingType_ParserBlockingInline,
          WebFeature::kScriptSchedulingType_InOrder,
          WebFeature::kScriptSchedulingType_Async,
          WebFeature::kClientHintsMetaHTTPEquivAcceptCH,
          WebFeature::kCookieDomainNonASCII,
          WebFeature::kClientHintsMetaEquivDelegateCH,
          WebFeature::kAuthorizationCoveredByWildcard,
          WebFeature::kImageAd,
          WebFeature::kLinkRelPrefetchAsDocumentSameOrigin,
          WebFeature::kLinkRelPrefetchAsDocumentCrossOrigin,
          WebFeature::kChromeLoadTimesCommitLoadTime,
          WebFeature::kChromeLoadTimesConnectionInfo,
          WebFeature::kChromeLoadTimesFinishDocumentLoadTime,
          WebFeature::kChromeLoadTimesFinishLoadTime,
          WebFeature::kChromeLoadTimesFirstPaintAfterLoadTime,
          WebFeature::kChromeLoadTimesFirstPaintTime,
          WebFeature::kChromeLoadTimesNavigationType,
          WebFeature::kChromeLoadTimesNpnNegotiatedProtocol,
          WebFeature::kChromeLoadTimesRequestTime,
          WebFeature::kChromeLoadTimesStartLoadTime,
          WebFeature::kChromeLoadTimesUnknown,
          WebFeature::kChromeLoadTimesWasAlternateProtocolAvailable,
          WebFeature::kChromeLoadTimesWasFetchedViaSpdy,
          WebFeature::kChromeLoadTimesWasNpnNegotiated,
          WebFeature::kGamepadButtons,
          WebFeature::kWebNfcNdefReaderScan,
          WebFeature::kWakeLockAcquireScreenLockWithoutStickyActivation,
          WebFeature::kDataUrlInSvgUse,
          WebFeature::kExecutedNonTrivialJavaScriptURL,
          WebFeature::kV8DeprecatedStorageQuota_QueryUsageAndQuota_Method,
          WebFeature::kV8DeprecatedStorageQuota_RequestQuota_Method,
          WebFeature::kRequestFileSystem,
          WebFeature::kRequestFileSystemWorker,
          WebFeature::kRequestFileSystemSyncWorker,
          WebFeature::kGetDisplayMediaWithPreferCurrentTabTrue,
          WebFeature::kV8Database_ChangeVersion_Method,
          WebFeature::kV8Database_Transaction_Method,
          WebFeature::kV8Database_ReadTransaction_Method,
          WebFeature::kV8SQLTransaction_ExecuteSql_Method,
          WebFeature::kMediaStreamOnActive,
          WebFeature::kMediaStreamOnInactive,
          WebFeature::kPrivacySandboxAdsAPIs,
          WebFeature::kV8Navigator_RunAdAuction_Method,
          WebFeature::kAttributionReportingAPIAll,
          WebFeature::kSharedStorageAPI_SharedStorage_DOMReference,
          WebFeature::kSharedStorageAPI_Run_Method,
          WebFeature::kSharedStorageAPI_SelectURL_Method,
          WebFeature::kSharedStorageAPI_SelectURL_Method_CalledWithOneURL,
          WebFeature::kTopicsAPI_BrowsingTopics_Method,
          WebFeature::kHTMLFencedFrameElement,
          WebFeature::kAuthorizationCrossOrigin,
          WebFeature::kCascadedCSSZoomNotEqualToOne,
          WebFeature::kV8Window_QueryLocalFonts_Method,
          WebFeature::kHiddenUntilFoundAttribute,
          WebFeature::kDanglingMarkupInWindowName,
          WebFeature::kWebGPURequestAdapter,
          WebFeature::kWebGPUQueueSubmit,
          WebFeature::kWebGPUCanvasContextGetCurrentTexture,
          WebFeature::kLinkRelPreloadAsFont,
          WebFeature::kV8Window_GetScreenDetails_Method,
          WebFeature::kV8Window_ShowSaveFilePicker_Method,
          WebFeature::kChromeCSIUnknown,
          WebFeature::kChromeCSIOnloadT,
          WebFeature::kChromeCSIPageT,
          WebFeature::kChromeCSIStartE,
          WebFeature::kChromeCSITran,
          WebFeature::kThirdPartyCookieAccessBlockByExperiment,
          WebFeature::kStorageAccessAPI_requestStorageAccess_BeyondCookies,
          WebFeature::kStorageAccessAPI_requestStorageAccess_BeyondCookies_all,
          WebFeature::
              kStorageAccessAPI_requestStorageAccess_BeyondCookies_sessionStorage,
          WebFeature::
              kStorageAccessAPI_requestStorageAccess_BeyondCookies_sessionStorage_Use,
          WebFeature::
              kStorageAccessAPI_requestStorageAccess_BeyondCookies_localStorage,
          WebFeature::
              kStorageAccessAPI_requestStorageAccess_BeyondCookies_localStorage_Use,
          WebFeature::
              kStorageAccessAPI_requestStorageAccess_BeyondCookies_indexedDB,
          WebFeature::
              kStorageAccessAPI_requestStorageAccess_BeyondCookies_indexedDB_Use,
          WebFeature::
              kStorageAccessAPI_requestStorageAccess_BeyondCookies_locks,
          WebFeature::
              kStorageAccessAPI_requestStorageAccess_BeyondCookies_locks_Use,
          WebFeature::
              kStorageAccessAPI_requestStorageAccess_BeyondCookies_caches,
          WebFeature::
              kStorageAccessAPI_requestStorageAccess_BeyondCookies_caches_Use,
          WebFeature::
              kStorageAccessAPI_requestStorageAccess_BeyondCookies_getDirectory,
          WebFeature::
              kStorageAccessAPI_requestStorageAccess_BeyondCookies_getDirectory_Use,
          WebFeature::kAdClickMainFrameNavigation,
          WebFeature::
              kStorageAccessAPI_requestStorageAccess_BeyondCookies_estimate,
          WebFeature::
              kStorageAccessAPI_requestStorageAccess_BeyondCookies_estimate_Use,
          WebFeature::
              kStorageAccessAPI_requestStorageAccess_BeyondCookies_createObjectURL,
          WebFeature::
              kStorageAccessAPI_requestStorageAccess_BeyondCookies_createObjectURL_Use,
          WebFeature::
              kStorageAccessAPI_requestStorageAccess_BeyondCookies_revokeObjectURL,
          WebFeature::
              kStorageAccessAPI_requestStorageAccess_BeyondCookies_revokeObjectURL_Use,
          WebFeature::
              kStorageAccessAPI_requestStorageAccess_BeyondCookies_BroadcastChannel,
          WebFeature::
              kStorageAccessAPI_requestStorageAccess_BeyondCookies_BroadcastChannel_Use,
          WebFeature::kThirdPartyCookieDeprecation_AllowByExplicitSetting,
          WebFeature::kThirdPartyCookieDeprecation_AllowByGlobalSetting,
          WebFeature::kThirdPartyCookieDeprecation_AllowBy3PCDMetadata,
          WebFeature::kThirdPartyCookieDeprecation_AllowBy3PCD,
          WebFeature::kThirdPartyCookieDeprecation_AllowBy3PCDHeuristics,
          WebFeature::kThirdPartyCookieDeprecation_AllowByStorageAccess,
          WebFeature::kThirdPartyCookieDeprecation_AllowByTopLevelStorageAccess,
          WebFeature::kAutoSpeculationRulesOptedOut,
          WebFeature::kOverrideFlashEmbedwithHTML,
          WebFeature::kElementRequestPointerLock,
          WebFeature::kKeyboardApiLock,
          WebFeature::kLCPImageWasLazy,
          WebFeature::kUserFeatureNgOptimizedImage,
          WebFeature::
              kThirdPartyCookieDeprecation_AllowByEnterprisePolicyCookieAllowedForUrls,
          WebFeature::kUserFeatureNgAfterRender,
          WebFeature::kUserFeatureNgHydration,
          WebFeature::kUserFeatureNextThirdPartiesGA,
          WebFeature::kUserFeatureNextThirdPartiesGTM,
          WebFeature::kUserFeatureNextThirdPartiesYouTubeEmbed,
          WebFeature::kUserFeatureNextThirdPartiesGoogleMapsEmbed,
          WebFeature::kStorageAccessAPI_hasUnpartitionedCookieAccess,
          WebFeature::
              kStorageAccessAPI_requestStorageAccess_BeyondCookies_cookies,
          WebFeature::kFirstPartySharedWorkerSameSiteCookiesNone,
          WebFeature::kUserFeatureNuxtImage,
          WebFeature::kUserFeatureNuxtPicture,
          WebFeature::kUserFeatureNuxtThirdPartiesGA,
          WebFeature::kUserFeatureNuxtThirdPartiesGTM,
          WebFeature::kUserFeatureNuxtThirdPartiesYouTubeEmbed,
          WebFeature::kUserFeatureNuxtThirdPartiesGoogleMaps,
          WebFeature::
              kStorageAccessAPI_requestStorageAccess_BeyondCookies_SharedWorker,
          WebFeature::
              kStorageAccessAPI_requestStorageAccess_BeyondCookies_SharedWorker_Use,
          WebFeature::kServiceWorkerStaticRouter_AddRoutes,
          WebFeature::kServiceWorkerStaticRouter_Evaluate,
          WebFeature::kNavigatorCookieEnabledThirdParty,
          WebFeature::kFedCmContinueOnResponse,
          WebFeature::kSchedulingIsInputPending,
          WebFeature::kV8DocumentPictureInPicture_RequestWindow_Method,
          WebFeature::
              kSharedStorageAPI_CreateWorklet_CrossOriginScriptDefaultDataOrigin,
          WebFeature::kSharedStorageAPI_AddModule_CrossOriginScript,
          WebFeature::kSharedStorageWriteFromBidderGenerateBid,
          WebFeature::kSharedStorageWriteFromBidderReportWin,
          WebFeature::kSharedStorageWriteFromSellerScoreAd,
          WebFeature::kSharedStorageWriteFromSellerReportResult,
          WebFeature::kIdentityDigitalCredentials,
          WebFeature::kIdentityDigitalCredentialsDeepLink,
          WebFeature::kV8FileSystemObserver_Observe_Method,
          WebFeature::kGamepadHapticActuatorType,
          WebFeature::kV8Navigator_JoinAdInterestGroup_Method,
          WebFeature::kFormValidationShowedMessage,
          WebFeature::kPartitionedPopin_OpenAttempt,
          WebFeature::kPartitionedPopin_Opened,
          WebFeature::kV8Window_PopinContextTypesSupported_Method,
          WebFeature::kV8Window_PopinContextType_Method,
          WebFeature::kSpeculationRulesHeader,
          WebFeature::kGeolocationGetCurrentPosition,
          WebFeature::kGeolocationWatchPosition,
          WebFeature::
              kServiceWorkerStaticRouter_RaceNetworkAndFetchHandlerImprovement,
          WebFeature::kThirdPartyCookieBlocked,
          WebFeature::kStorageAccessAPI_requestStorageAccessFor_Method,
          WebFeature::kSharedWorkerScriptUnderServiceWorkerControlIsBlob,
          WebFeature::kCSSEnvironmentVariable_SafeAreaInsetBottom_FastPath,
          WebFeature::kAdScriptInStackOnGeoLocation,
          WebFeature::kAdScriptInStackOnClipboardRead,
          WebFeature::kAdScriptInStackOnBluetooth,
          WebFeature::kAdScriptInStackOnMicrophoneRead,
          WebFeature::kAdScriptInStackOnCameraRead,
          WebFeature::kUpgradeInsecureRequestsUpgradedRequestBlockable,
          WebFeature::kMediaSessionEnterPictureInPicture,
          WebFeature::kV8WasmMultiMemory,
          WebFeature::kV8WasmMemory64,
          WebFeature::kV8WasmGC,
          WebFeature::kV8WebAssemblyJSStringBuiltins,
          WebFeature::kV8WasmJavaScriptPromiseIntegration,
          WebFeature::kV8WasmReturnCall,
          WebFeature::kV8WasmExnRef,
          WebFeature::kV8WasmExceptionHandling,
          WebFeature::kTextAutosizing,
          WebFeature::kCredentialManagerGetPasswordCredential,
          WebFeature::kGeolocationWouldSucceedWhenAdScriptInStack,
          WebFeature::kAdScriptInStackOnWatchGeoLocation,
          WebFeature::kCrossPartitionSameOriginBlobURLFetch,
          WebFeature::kCSPBlockedWorkerCreation,
          WebFeature::kCrossOriginOwnerInterestGroupSubframeCheckFailed,
          WebFeature::kV8Navigator_LeaveAdInterestGroup_Method,
          WebFeature::kV8Navigator_ClearOriginJoinedAdInterestGroups_Method,
          WebFeature::kLanguageDetector_MeasureInputUsage,
          WebFeature::kLanguageDetector_InputQuota,
          WebFeature::kTranslator_Create,
          WebFeature::kTranslator_Availability,
          WebFeature::kTranslator_SourceLanguage,
          WebFeature::kTranslator_TargetLanguage,
          WebFeature::kTranslator_Destroy,
          WebFeature::kTranslator_Translate,
          WebFeature::kTranslator_TranslateStreaming,
          WebFeature::kTranslator_MeasureInputUsage,
          WebFeature::kTranslator_InputQuota,
          WebFeature::kLanguageDetector_Create,
          WebFeature::kLanguageDetector_Availability,
          WebFeature::kLanguageDetector_Detect,
          WebFeature::kLanguageDetector_Destroy,
          WebFeature::kSummarizer_Create,
          WebFeature::kSummarizer_Summarize,
          WebFeature::kSummarizer_SummarizeStreaming,
          WebFeature::kWriter_Create,
          WebFeature::kWriter_Write,
          WebFeature::kWriter_WriteStreaming,
          WebFeature::kRewriter_Create,
          WebFeature::kRewriter_Rewrite,
          WebFeature::kRewriter_RewriteStreaming,
          WebFeature::kLanguageModel_Create,
          WebFeature::kLanguageModel_Prompt,
          WebFeature::kLanguageModel_PromptStreaming,
          WebFeature::kLanguageModel_Prompt_Input_Image,
          WebFeature::kLanguageModel_Prompt_Input_Audio,
          WebFeature::kCrossOriginSameSiteCookieAccessViaStorageAccessAPI,
          WebFeature::kCredentialManagerCreateFederatedCredential,
          WebFeature::kCredentialManagerStoreFederatedCredential,
          WebFeature::kCredentialManagerGetLegacyFederatedCredential,
          WebFeature::kClearSiteData,
          // NOTE: before adding new use counters here, verify in UMA that their
          // emissions are very rare, e.g. <1% of page loads.
      }));
  return *opt_in_features;
}
