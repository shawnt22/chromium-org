// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/test/scoped_feature_list.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/extensions/chrome_test_extension_loader.h"
#include "chrome/browser/extensions/install_verifier.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_element_identifiers.h"
#include "chrome/browser/ui/views/new_tab_footer/footer_web_view.h"
#include "chrome/browser/ui/webui/test_support/webui_interactive_test_mixin.h"
#include "chrome/common/webui_url_constants.h"
#include "chrome/test/interaction/interactive_browser_test.h"
#include "components/search/ntp_features.h"
#include "components/themes/ntp_background_data.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/url_loader_interceptor.h"
#include "extensions/test/test_extension_dir.h"

namespace {
DEFINE_LOCAL_ELEMENT_IDENTIFIER_VALUE(kNewTabElementId);
DEFINE_LOCAL_CUSTOM_ELEMENT_EVENT_TYPE(kElementExists);

class CustomizeChromeInteractiveTest
    : public WebUiInteractiveTestMixin<InteractiveBrowserTest> {
 public:
  CustomizeChromeInteractiveTest() {
    scoped_feature_list_.InitWithFeatures(
        {ntp_features::kNtpFooter},
        {ntp_features::kNtpBackgroundImageErrorDetection});
  }

  InteractiveTestApi::MultiStep ClickElement(
      const ui::ElementIdentifier& contents_id,
      const DeepQuery& element) {
    return Steps(WaitForElementToRender(contents_id, element),
                 ExecuteJsAt(contents_id, element, "el => el.click()"));
  }

  InteractiveTestApi::MultiStep WaitForElementExists(
      const ui::ElementIdentifier& contents_id,
      const DeepQuery& element) {
    StateChange element_exists;
    element_exists.type =
        WebContentsInteractionTestUtil::StateChange::Type::kExists;
    element_exists.event = kElementExists;
    element_exists.where = element;
    return WaitForStateChange(contents_id, element_exists);
  }

  InteractiveTestApi::MultiStep WaitForElementDoesNotExist(
      const ui::ElementIdentifier& contents_id,
      const DeepQuery& element) {
    StateChange element_does_not_exist;
    element_does_not_exist.type =
        WebContentsInteractionTestUtil::StateChange::Type::kDoesNotExist;
    element_does_not_exist.event = kElementExists;
    element_does_not_exist.where = element;
    return WaitForStateChange(contents_id, element_does_not_exist);
  }

  InteractiveTestApi::MultiStep OpenCustomizeChromeSidePanel(
      const ui::ElementIdentifier& contents_id) {
    return Steps(Do(base::BindLambdaForTesting([=, this]() {
                   chrome::ExecuteCommand(browser(),
                                          IDC_SHOW_CUSTOMIZE_CHROME_SIDE_PANEL);
                 })),
                 WaitForShow(kCustomizeChromeSidePanelWebViewElementId),
                 InstrumentNonTabWebView(
                     contents_id, kCustomizeChromeSidePanelWebViewElementId));
  }

  // Loads an extensions overriding the NTP. `index` is used to differentiate
  // multiple installed extensions.
  void LoadNtpOverridingExtension(int index = 0) {
    extensions::TestExtensionDir extension_dir;
    extension_dir.WriteFile(FILE_PATH_LITERAL("ext.html"),
                            "<body>Extension-overridden NTP</body>");

    const char extension_manifest[] = R"(
        {
            "chrome_url_overrides": {
                "newtab": "ext.html"
            },
            "name": "Extension-overridden NTP %d",
            "manifest_version": 3,
            "version": "0.1"
          })";

    extension_dir.WriteManifest(absl::StrFormat(extension_manifest, index));

    extensions::ChromeTestExtensionLoader extension_loader(
        browser()->profile());
    extension_loader.set_ignore_manifest_warnings(true);
    const extensions::Extension* extension =
        extension_loader.LoadExtension(extension_dir.Pack()).get();
    ASSERT_TRUE(extension);
  }

  std::unique_ptr<content::URLLoaderInterceptor> SetUpThemesResponses() {
    return std::make_unique<content::URLLoaderInterceptor>(
        base::BindLambdaForTesting(
            [&](content::URLLoaderInterceptor::RequestParams* params) -> bool {
              if (params->url_request.url.path() ==
                  "/cast/chromecast/home/wallpaper/collections") {
                std::string headers =
                    "HTTP/1.1 200 OK\nContent-Type: application/json\n\n";
                ntp::background::Collection collection;
                collection.set_collection_id("shapes");
                collection.set_collection_name("Shapes");
                collection.add_preview()->set_image_url(
                    "https://wallpapers.co/some_image");
                ntp::background::GetCollectionsResponse response;
                *response.add_collections() = collection;
                std::string response_string;
                response.SerializeToString(&response_string);
                content::URLLoaderInterceptor::WriteResponse(
                    headers, response_string, params->client.get(),
                    std::optional<net::SSLInfo>());
                return true;
              } else if (params->url_request.url.path() ==
                         "/cast/chromecast/home/wallpaper/collection-images") {
                std::string headers =
                    "HTTP/1.1 200 OK\nContent-Type: application/json\n\n";
                ntp::background::Image image;
                image.set_asset_id(12345);
                image.set_image_url("https://wallpapers.co/some_image");
                image.add_attribution()->set_text("attribution text");
                ntp::background::GetImagesInCollectionResponse response;
                *response.add_images() = image;
                std::string response_string;
                response.SerializeToString(&response_string);
                content::URLLoaderInterceptor::WriteResponse(
                    headers, response_string, params->client.get(),
                    std::optional<net::SSLInfo>());
                return true;
              }
              return false;
            }));
  }

 protected:
  base::test::ScopedFeatureList scoped_feature_list_;
  extensions::ScopedInstallVerifierBypassForTest install_verifier_bypass_;
};
}  // namespace

IN_PROC_BROWSER_TEST_F(CustomizeChromeInteractiveTest,
                       ShowsFooterSectionForExtensionNtp) {
  DEFINE_LOCAL_ELEMENT_IDENTIFIER_VALUE(kLocalCustomizeChromeElementId);
  const DeepQuery kFooterSection = {"customize-chrome-app", "#footer",
                                    "customize-chrome-footer",
                                    "#showToggleContainer"};
  // Load extension that overrides NTP.
  LoadNtpOverridingExtension();
  RunTestSequence(
      // Open extension new tab page.
      AddInstrumentedTab(kNewTabElementId, GURL(chrome::kChromeUINewTabURL)),
      // Open customize chrome side panel.
      OpenCustomizeChromeSidePanel(kLocalCustomizeChromeElementId),
      // Check that the footer section exists.
      Steps(
          WaitForElementExists(kLocalCustomizeChromeElementId, kFooterSection),
          WaitForElementToRender(kLocalCustomizeChromeElementId,
                                 kFooterSection)));
}

IN_PROC_BROWSER_TEST_F(CustomizeChromeInteractiveTest,
                       FooterSectionNotShownForNonExtensionNtp) {
  DEFINE_LOCAL_ELEMENT_IDENTIFIER_VALUE(kLocalCustomizeChromeElementId);
  const DeepQuery kFooterSection = {"customize-chrome-app", "#footer",
                                    "customize-chrome-footer",
                                    "#showToggleContainer"};
  RunTestSequence(
      // Open non-extension new tab page.
      AddInstrumentedTab(kNewTabElementId, GURL(chrome::kChromeUINewTabURL)),
      // Open customize chrome side panel.
      OpenCustomizeChromeSidePanel(kLocalCustomizeChromeElementId),
      // Check that the footer section does not exist.
      EnsureNotPresent(kLocalCustomizeChromeElementId, kFooterSection));
}

IN_PROC_BROWSER_TEST_F(CustomizeChromeInteractiveTest,
                       TogglesFooterVisibility) {
  DEFINE_LOCAL_ELEMENT_IDENTIFIER_VALUE(kLocalCustomizeChromeElementId);
  const DeepQuery kFooterToggle = {"customize-chrome-app", "#footer",
                                   "customize-chrome-footer",
                                   "#showToggleContainer"};
  // Install extension NTP.
  LoadNtpOverridingExtension();
  RunTestSequence(
      // Open NTP with footer showing.
      Steps(AddInstrumentedTab(kNewTabElementId,
                               GURL(chrome::kChromeUINewTabURL)),
            WaitForShow(kNtpFooterId)),
      // Click the footer toggle.
      Steps(OpenCustomizeChromeSidePanel(kLocalCustomizeChromeElementId),
            ClickElement(kLocalCustomizeChromeElementId, kFooterToggle)),
      // Ensure footer hides.
      WaitForHide(kNtpFooterId),
      // Click the footer toggle.
      ClickElement(kLocalCustomizeChromeElementId, kFooterToggle),
      // Ensure footer shows.
      WaitForShow(kNtpFooterId));
}

IN_PROC_BROWSER_TEST_F(CustomizeChromeInteractiveTest,
                       EditThemeDisablesExtensionNtps) {
  std::unique_ptr<content::URLLoaderInterceptor> fetch_interceptor =
      SetUpThemesResponses();

  DEFINE_LOCAL_ELEMENT_IDENTIFIER_VALUE(kLocalCustomizeChromeElementId);
  DEFINE_LOCAL_ELEMENT_IDENTIFIER_VALUE(kLocalNewTabElementId);

  DEFINE_LOCAL_CUSTOM_ELEMENT_EVENT_TYPE(kNtpHasBackgroundEvent);
  StateChange ntp_has_background;
  ntp_has_background.type = StateChange::Type::kExistsAndConditionTrue;
  ntp_has_background.where = {"body"};
  ntp_has_background.event = kNtpHasBackgroundEvent;
  ntp_has_background.test_function =
      "(el) => el.hasAttribute('show-background-image')";

  const DeepQuery kEditThemeButton = {"customize-chrome-app",
                                      "#appearanceElement", "#editThemeButton"};
  const DeepQuery kCollectionButton = {"customize-chrome-app",
                                       "#categoriesPage", ".collection"};
  const DeepQuery kThemeButton = {"customize-chrome-app", "#themesPage",
                                  ".theme"};
  const DeepQuery kFooterSection = {"customize-chrome-app", "#footer",
                                    "customize-chrome-footer",
                                    "#showToggleContainer"};

  // Install multiple extensions that override the new tab page.
  LoadNtpOverridingExtension(0);
  LoadNtpOverridingExtension(1);
  RunTestSequence(
      // Open extension NTP.
      AddInstrumentedTab(kNewTabElementId, GURL(chrome::kChromeUINewTabURL)),
      // Choose a 1P NTP theme from Customize Chrome.
      Steps(OpenCustomizeChromeSidePanel(kLocalCustomizeChromeElementId),
            ClickElement(kLocalCustomizeChromeElementId, kEditThemeButton),
            ClickElement(kLocalCustomizeChromeElementId, kCollectionButton),
            ClickElement(kLocalCustomizeChromeElementId, kThemeButton)),
      // Ensure that both extensions were disabled, and the tab redirected to 1P
      // NTP with background.
      Steps(WaitForWebContentsNavigation(kNewTabElementId,
                                         GURL(chrome::kChromeUINewTabURL)),
            WaitForStateChange(kNewTabElementId, ntp_has_background)),
      // Ensure that opening a new tab redirects to the 1P NTP.
      Steps(AddInstrumentedTab(kLocalNewTabElementId,
                               GURL(chrome::kChromeUINewTabURL)),
            WaitForStateChange(kLocalNewTabElementId, ntp_has_background)));
}
