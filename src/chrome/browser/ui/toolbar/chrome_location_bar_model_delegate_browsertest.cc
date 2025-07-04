// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/toolbar/chrome_location_bar_model_delegate.h"

#include <memory>

#include "base/memory/raw_ptr.h"
#include "base/test/scoped_feature_list.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/search/search.h"
#include "chrome/browser/search_engines/template_url_service_factory.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/common/url_constants.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/search_test_utils.h"
#include "components/search_engines/template_url_service.h"
#include "content/public/test/browser_test.h"

// Concrete implementation of ChromeLocationBarModelDelegate.
class TestChromeLocationBarModelDelegate
    : public ChromeLocationBarModelDelegate {
 public:
  explicit TestChromeLocationBarModelDelegate(Browser* browser)
      : browser_(browser) {}
  ~TestChromeLocationBarModelDelegate() override = default;

  // Not copyable or movable.
  TestChromeLocationBarModelDelegate(
      const TestChromeLocationBarModelDelegate&) = delete;
  TestChromeLocationBarModelDelegate& operator=(
      const TestChromeLocationBarModelDelegate&) = delete;

  // ChromeLocationBarModelDelegate:
  content::WebContents* GetActiveWebContents() const override {
    return browser_->tab_strip_model()->GetActiveWebContents();
  }

  std::unique_ptr<security_state::VisibleSecurityState>
  GetVisibleSecurityState() const override {
    auto state = std::make_unique<security_state::VisibleSecurityState>();
    state->cert_status = cert_status_;
    return state;
  }

  void set_cert_status(net::CertStatus cert_status) {
    cert_status_ = cert_status;
  }

 private:
  const raw_ptr<Browser, DanglingUntriaged> browser_;
  net::CertStatus cert_status_ = 0;
};

class ChromeLocationBarModelDelegateTest : public InProcessBrowserTest {
 protected:
  ChromeLocationBarModelDelegateTest() = default;

  void SetUpBrowserContextKeyedServices(
      content::BrowserContext* context) override {
    TemplateURLServiceFactory::GetInstance()->SetTestingFactory(
        context,
        base::BindRepeating(&TemplateURLServiceFactory::BuildInstanceFor));
  }

  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();

    TemplateURLService* template_url_service =
        TemplateURLServiceFactory::GetForProfile(browser()->profile());
    search_test_utils::WaitForTemplateURLServiceToLoad(template_url_service);

    delegate_ = std::make_unique<TestChromeLocationBarModelDelegate>(browser());
  }

  void SetSearchProvider(bool set_ntp_url) {
    TemplateURLService* template_url_service =
        TemplateURLServiceFactory::GetForProfile(browser()->profile());
    TemplateURLData data;
    data.SetShortName(u"foo.com");
    data.SetURL("http://foo.com/url?bar={searchTerms}");
    if (set_ntp_url) {
      data.new_tab_url = "https://foo.com/newtab";
    }

    TemplateURL* template_url =
        template_url_service->Add(std::make_unique<TemplateURL>(data));
    template_url_service->SetUserSelectedDefaultSearchProvider(template_url);
  }

  GURL GetURL() {
    GURL url;
    EXPECT_TRUE(delegate_->GetURL(&url));
    return url;
  }

  TestChromeLocationBarModelDelegate* delegate() { return delegate_.get(); }

 private:
  base::test::ScopedFeatureList scoped_feature_list_;
  std::unique_ptr<TestChromeLocationBarModelDelegate> delegate_;
};

// Tests whether ChromeLocationBarModelDelegate::IsNewTabPage and
// ChromeLocationBarModelDelegate::IsNewTabPageURL return the expected results
// for various NTP scenarios.
IN_PROC_BROWSER_TEST_F(ChromeLocationBarModelDelegateTest, IsNewTabPage) {
  chrome::NewTab(browser());
  // New Tab URL with Google DSP resolves to the local or the WebUI NTP URL.
  GURL ntp_url(chrome::kChromeUINewTabPageURL);
  EXPECT_EQ(ntp_url, search::GetNewTabPageURL(browser()->profile()));

  EXPECT_TRUE(delegate()->IsNewTabPage());
  EXPECT_TRUE(delegate()->IsNewTabPageURL(GetURL()));

  SetSearchProvider(false);
  chrome::NewTab(browser());
  // New Tab URL with a user selected DSP without an NTP URL resolves to
  // chrome://new-tab-page-third-party/.
  EXPECT_EQ(GURL(chrome::kChromeUINewTabPageThirdPartyURL),
            search::GetNewTabPageURL(browser()->profile()));

  EXPECT_FALSE(delegate()->IsNewTabPage());
  EXPECT_TRUE(delegate()->IsNewTabPageURL(GetURL()));

  SetSearchProvider(true);
  chrome::NewTab(browser());
  // New Tab URL with a user selected DSP resolves to the DSP's NTP URL.
  EXPECT_EQ("https://foo.com/newtab",
            search::GetNewTabPageURL(browser()->profile()));

  EXPECT_FALSE(delegate()->IsNewTabPage());
  EXPECT_TRUE(delegate()->IsNewTabPageURL(GetURL()));
}

IN_PROC_BROWSER_TEST_F(ChromeLocationBarModelDelegateTest,
                       CertErrorPreventsElision) {
  EXPECT_FALSE(delegate()->ShouldPreventElision());
  delegate()->set_cert_status(net::CERT_STATUS_REVOKED);
  EXPECT_TRUE(delegate()->ShouldPreventElision());
}
