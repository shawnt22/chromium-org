// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/manifest_v2_experiment_manager.h"

#include "base/one_shot_event.h"
#include "base/strings/stringprintf.h"
#include "base/test/bind.h"
#include "base/test/metrics/histogram_tester.h"
#include "base/test/scoped_feature_list.h"
#include "chrome/browser/extensions/extension_browsertest.h"
#include "chrome/browser/extensions/extension_management_internal.h"
#include "chrome/browser/extensions/external_provider_manager.h"
#include "chrome/browser/extensions/mv2_experiment_stage.h"
#include "chrome/browser/extensions/unpacked_installer.h"
#include "chrome/browser/profiles/profile.h"
#include "components/ukm/test_ukm_recorder.h"
#include "content/public/test/browser_test.h"
#include "extensions/browser/extension_prefs.h"
#include "extensions/browser/extension_registrar.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/browser/extension_system.h"
#include "extensions/browser/mock_external_provider.h"
#include "extensions/browser/pref_names.h"
#include "extensions/browser/test_extension_registry_observer.h"
#include "extensions/common/extension.h"
#include "extensions/common/extension_features.h"
#include "extensions/common/feature_switch.h"
#include "extensions/common/mojom/manifest.mojom.h"
#include "extensions/test/test_extension_dir.h"
#include "services/metrics/public/cpp/ukm_builders.h"
#include "services/metrics/public/cpp/ukm_recorder.h"

namespace extensions {
namespace {

const Extension* GetExtensionByName(std::string_view name,
                                    const ExtensionSet& extensions) {
  const Extension* extension = nullptr;
  for (const auto& e : extensions) {
    if (e->name() == name) {
      extension = e.get();
      break;
    }
  }

  return extension;
}

// Each test may have a different desired stage. Store them here so the test
// harness properly instantiates them.
MV2ExperimentStage GetExperimentStageForTest(std::string_view test_name) {
  struct {
    const char* test_name;
    MV2ExperimentStage stage;
  } test_stages[] = {
      {"PRE_PRE_ExtensionsAreDisabledOnStartup", MV2ExperimentStage::kWarning},
      {"PRE_ExtensionsAreDisabledOnStartup", MV2ExperimentStage::kWarning},
      {"ExtensionsAreDisabledOnStartup",
       MV2ExperimentStage::kDisableWithReEnable},
      {"PRE_PRE_ExtensionsCanBeReEnabledByUsers", MV2ExperimentStage::kWarning},
      {"PRE_ExtensionsCanBeReEnabledByUsers",
       MV2ExperimentStage::kDisableWithReEnable},
      {"ExtensionsCanBeReEnabledByUsers",
       MV2ExperimentStage::kDisableWithReEnable},
      {"ExtensionsAreReEnabledWhenUpdatedToMV3",
       MV2ExperimentStage::kDisableWithReEnable},
      {"PRE_MarkingNoticeAsAcknowledged", MV2ExperimentStage::kWarning},
      {"MarkingNoticeAsAcknowledged", MV2ExperimentStage::kDisableWithReEnable},
      {"PRE_MarkingGlobalNoticeAsAcknowledged", MV2ExperimentStage::kWarning},
      {"MarkingGlobalNoticeAsAcknowledged",
       MV2ExperimentStage::kDisableWithReEnable},
      {"PRE_PRE_ExtensionsAreReEnabledIfExperimentDisabled",
       MV2ExperimentStage::kWarning},
      {"PRE_ExtensionsAreReEnabledIfExperimentDisabled",
       MV2ExperimentStage::kDisableWithReEnable},
      {"ExtensionsAreReEnabledIfExperimentDisabled",
       MV2ExperimentStage::kWarning},
      {"ExternalExtensionsCanBeInstalledButAreAlsoDisabled",
       MV2ExperimentStage::kDisableWithReEnable},
      {"UkmIsEmittedForExtensionWhenUninstalled",
       MV2ExperimentStage::kDisableWithReEnable},
      {"UkmIsNotEmittedForOtherUninstallations",
       MV2ExperimentStage::kDisableWithReEnable},
      {"PRE_MV2ExtensionsAreNotDisabledIfLegacyExtensionSwitchIsApplied",
       MV2ExperimentStage::kWarning},
      {"MV2ExtensionsAreNotDisabledIfLegacyExtensionSwitchIsApplied",
       MV2ExperimentStage::kUnsupported},
      {"PRE_PRE_FlowFromWarningToUnsupported", MV2ExperimentStage::kWarning},
      {"PRE_FlowFromWarningToUnsupported",
       MV2ExperimentStage::kDisableWithReEnable},
      {"FlowFromWarningToUnsupported", MV2ExperimentStage::kUnsupported},
      {"UnpackedExtensionsCanBeInstalledInDisabledPhase",
       MV2ExperimentStage::kDisableWithReEnable},
      {"UnpackedExtensionsCannotBeInstalledInUnsupportedPhase",
       MV2ExperimentStage::kUnsupported},
  };

  for (const auto& test_stage : test_stages) {
    if (test_stage.test_name == test_name) {
      return test_stage.stage;
    }
  }

  NOTREACHED()
      << "Unknown test name '" << test_name << "'. "
      << "You need to add a new test stage entry into this collection.";
}

}  // namespace

class ManifestV2ExperimentManagerBrowserTest : public ExtensionBrowserTest {
 public:
  ManifestV2ExperimentManagerBrowserTest() = default;
  ~ManifestV2ExperimentManagerBrowserTest() override = default;

  void SetUp() override {
    std::vector<base::test::FeatureRef> enabled_features;
    std::vector<base::test::FeatureRef> disabled_features;

    // Each test may need a different value for the experiment stages, since
    // many need some kind of pre-experiment set up, then test the behavior on
    // subsequent startups. Initialize each test according to its preferred
    // stage.
    MV2ExperimentStage experiment_stage = GetExperimentStageForTest(
        testing::UnitTest::GetInstance()->current_test_info()->name());

    switch (experiment_stage) {
      case MV2ExperimentStage::kWarning:
        enabled_features.push_back(
            extensions_features::kExtensionManifestV2DeprecationWarning);
        disabled_features.push_back(
            extensions_features::kExtensionManifestV2Disabled);
        disabled_features.push_back(
            extensions_features::kExtensionManifestV2Unsupported);
        break;
      case MV2ExperimentStage::kDisableWithReEnable:
        enabled_features.push_back(
            extensions_features::kExtensionManifestV2Disabled);
        disabled_features.push_back(
            extensions_features::kExtensionManifestV2DeprecationWarning);
        disabled_features.push_back(
            extensions_features::kExtensionManifestV2Unsupported);
        break;
      case MV2ExperimentStage::kUnsupported:
        enabled_features.push_back(
            extensions_features::kExtensionManifestV2Unsupported);
        disabled_features.push_back(
            extensions_features::kExtensionManifestV2DeprecationWarning);
        disabled_features.push_back(
            extensions_features::kExtensionManifestV2Disabled);
        break;
      case MV2ExperimentStage::kNone:
        NOTREACHED() << "Unhandled stage.";
    }

    PopulateAdditionalFeatures(enabled_features, disabled_features);

    feature_list_.InitWithFeatures(enabled_features, disabled_features);

    ExtensionBrowserTest::SetUp();
  }

  void TearDown() override {
    ExtensionBrowserTest::TearDown();
  }

  void SetUpCommandLine(base::CommandLine* command_line) override {
    ExtensionBrowserTest::SetUpCommandLine(command_line);
  }

  void SetUpOnMainThread() override {
    ExtensionBrowserTest::SetUpOnMainThread();

    ukm_recorder_.emplace();
    // UKM only emits for webstore extensions. Pretend any extension is a store
    // extension for this test.
    ukm_recorder_->SetIsWebstoreExtensionCallback(
        base::BindRepeating([](std::string_view) { return true; }));
  }

  // Since this is testing the MV2 deprecation experiments, we don't want to
  // bypass their disabling for testing.
  bool ShouldAllowMV2Extensions() override { return false; }

  void WaitForExtensionSystemReady() {
    base::RunLoop run_loop;
    ExtensionSystem::Get(profile())->ready().Post(
        FROM_HERE, run_loop.QuitWhenIdleClosure());
    run_loop.Run();
  }

  // Uninstalls the extension with the given `extension_id` and for the given
  // `uninstall_reason`, waiting until uninstallation has finished.
  void UninstallExtension(const ExtensionId& extension_id,
                          UninstallReason uninstall_reason) {
    base::RunLoop run_loop;
    extension_registrar()->UninstallExtension(extension_id, uninstall_reason,
                                              /*error=*/nullptr,
                                              run_loop.QuitWhenIdleClosure());
    run_loop.Run();
  }

  // Adds a new MV2 extension with the given `name` to the profile, returning
  // it afterwards.
  const Extension* AddMV2Extension(std::string_view name) {
    return AddExtensionWithManifestVersion(name, 2);
  }

  const Extension* AddExtensionWithManifestVersion(std::string_view name,
                                                   int manifest_version) {
    static constexpr char kManifest[] =
        R"({
             "name": "%s",
             "manifest_version": %d,
             "version": "0.1"
           })";
    TestExtensionDir test_dir;
    test_dir.WriteManifest(
        base::StringPrintf(kManifest, name.data(), manifest_version));
    return InstallExtension(test_dir.UnpackedPath(), /*expected_change=*/1,
                            mojom::ManifestLocation::kInternal);
  }

  // Returns true if the extension was explicitly re-enabled by the user after
  // being disabled by the MV2 experiment.
  bool WasExtensionReEnabledByUser(const ExtensionId& extension_id) {
    return experiment_manager()->DidUserReEnableExtensionForTesting(
        extension_id);
  }

  // Returns the UKM entries for the Extensions.MV2ExtensionHandledInSoftDisable
  // event.
  std::vector<raw_ptr<const ukm::mojom::UkmEntry, VectorExperimental>>
  GetUkmEntries() {
    return ukm_recorder().GetEntriesByName(
        ukm::builders::Extensions_MV2ExtensionHandledInSoftDisable::kEntryName);
  }

  MV2ExperimentStage GetActiveExperimentStage() {
    return experiment_manager()->GetCurrentExperimentStage();
  }

  ExtensionPrefs* extension_prefs() { return ExtensionPrefs::Get(profile()); }

  ManifestV2ExperimentManager* experiment_manager() {
    return ManifestV2ExperimentManager::Get(profile());
  }

  base::HistogramTester& histogram_tester() { return histogram_tester_; }
  ukm::TestAutoSetUkmRecorder& ukm_recorder() { return *ukm_recorder_; }

 private:
  // Allows subclasses to add additional features to enable / disable.
  virtual void PopulateAdditionalFeatures(
      std::vector<base::test::FeatureRef>& enabled_features,
      std::vector<base::test::FeatureRef>& disabled_features) {}

  base::test::ScopedFeatureList feature_list_;
  base::HistogramTester histogram_tester_;
  std::optional<ukm::TestAutoSetUkmRecorder> ukm_recorder_;
};

// A test series to verify MV2 extensions are disabled on startup.
// Step 1 (Warning Only Stage): Install an MV2 extension.
IN_PROC_BROWSER_TEST_F(ManifestV2ExperimentManagerBrowserTest,
                       PRE_PRE_ExtensionsAreDisabledOnStartup) {
  EXPECT_EQ(MV2ExperimentStage::kWarning, GetActiveExperimentStage());

  const Extension* extension = AddMV2Extension("Test MV2 Extension");
  ASSERT_TRUE(extension);
}
// Step 2 (Warning Only Stage): Verify the MV2 extension is still enabled after
// restarting the browser. Since this is still a PRE_ stage, the disabling
// experiment isn't active, and MV2 extensions should be unaffected.
IN_PROC_BROWSER_TEST_F(ManifestV2ExperimentManagerBrowserTest,
                       PRE_ExtensionsAreDisabledOnStartup) {
  EXPECT_EQ(MV2ExperimentStage::kWarning, GetActiveExperimentStage());

  WaitForExtensionSystemReady();

  const Extension* extension = GetExtensionByName(
      "Test MV2 Extension", extension_registry()->enabled_extensions());

  ASSERT_TRUE(extension);
  const ExtensionId extension_id = extension->id();
  EXPECT_TRUE(
      extension_registry()->enabled_extensions().Contains(extension_id));

  EXPECT_TRUE(extension_prefs()->GetDisableReasons(extension_id).empty());

  histogram_tester().ExpectTotalCount(
      "Extensions.MV2Deprecation.MV2ExtensionState.Internal", 0);
}
// Step 3 (Disable Stage): Verify the extension is disabled. Now the disabling
// experiment is active, and any old MV2 extensions are disabled.
IN_PROC_BROWSER_TEST_F(ManifestV2ExperimentManagerBrowserTest,
                       ExtensionsAreDisabledOnStartup) {
  EXPECT_EQ(MV2ExperimentStage::kDisableWithReEnable,
            GetActiveExperimentStage());

  WaitForExtensionSystemReady();

  const Extension* extension = GetExtensionByName(
      "Test MV2 Extension",
      extension_registry()->GenerateInstalledExtensionsSet());

  ASSERT_TRUE(extension);
  const ExtensionId extension_id = extension->id();

  EXPECT_FALSE(
      extension_registry()->enabled_extensions().Contains(extension_id));
  EXPECT_TRUE(
      extension_registry()->disabled_extensions().Contains(extension_id));

  EXPECT_THAT(extension_prefs()->GetDisableReasons(extension_id),
              testing::UnorderedElementsAre(
                  disable_reason::DISABLE_UNSUPPORTED_MANIFEST_VERSION));

  // The extension is recorded as "soft disabled".
  histogram_tester().ExpectTotalCount(
      "Extensions.MV2Deprecation.MV2ExtensionState.Internal", 1);
  histogram_tester().ExpectBucketCount(
      "Extensions.MV2Deprecation.MV2ExtensionState.Internal",
      ManifestV2ExperimentManager::MV2ExtensionState::kSoftDisabled, 1);
}

// A test series to verify extensions that are re-enabled by the user do not
// get re-disabled on subsequent starts.
// Step 1 (Warning Only Stage): Install an MV2 extension.
IN_PROC_BROWSER_TEST_F(ManifestV2ExperimentManagerBrowserTest,
                       PRE_PRE_ExtensionsCanBeReEnabledByUsers) {
  EXPECT_EQ(MV2ExperimentStage::kWarning, GetActiveExperimentStage());

  const Extension* extension = AddMV2Extension("Test MV2 Extension");
  ASSERT_TRUE(extension);
}
// Step 2 (Disable Stage): The extension will be disabled by the experiment.
// Re-enable the extension.
IN_PROC_BROWSER_TEST_F(ManifestV2ExperimentManagerBrowserTest,
                       PRE_ExtensionsCanBeReEnabledByUsers) {
  EXPECT_EQ(MV2ExperimentStage::kDisableWithReEnable,
            GetActiveExperimentStage());

  WaitForExtensionSystemReady();

  const Extension* extension = GetExtensionByName(
      "Test MV2 Extension", extension_registry()->disabled_extensions());
  ASSERT_TRUE(extension);
  const ExtensionId extension_id = extension->id();

  // Before re-enabling the extension, there should be no UKM entries.
  EXPECT_TRUE(GetUkmEntries().empty());

  // Re-enable the disabled extension.
  extension_registrar()->EnableExtension(extension_id);

  // The extension should be properly re-enabled, the disable reasons cleared,
  // and the extension should be marked as explicitly re-enabled.
  EXPECT_TRUE(
      extension_registry()->enabled_extensions().Contains(extension_id));
  EXPECT_TRUE(extension_prefs()->GetDisableReasons(extension_id).empty());
  EXPECT_TRUE(WasExtensionReEnabledByUser(extension_id));

  // We should emit a UKM record for the re-enabling.
  auto entries = GetUkmEntries();
  ASSERT_EQ(1u, entries.size());
  auto* entry = entries.front().get();
  ukm_recorder().ExpectEntrySourceHasUrl(entry, extension->url());
  ukm_recorder().ExpectEntryMetric(
      entry,
      ukm::builders::Extensions_MV2ExtensionHandledInSoftDisable::kActionName,
      static_cast<int64_t>(ManifestV2ExperimentManager::
                               ExtensionMV2DeprecationAction::kReEnabled));
}
// Step 3 (Disable Stage): The extension should still be enabled on a subsequent
// start since the user explicitly chose to re-enable it.
IN_PROC_BROWSER_TEST_F(ManifestV2ExperimentManagerBrowserTest,
                       ExtensionsCanBeReEnabledByUsers) {
  EXPECT_EQ(MV2ExperimentStage::kDisableWithReEnable,
            GetActiveExperimentStage());

  WaitForExtensionSystemReady();

  const Extension* extension = GetExtensionByName(
      "Test MV2 Extension", extension_registry()->enabled_extensions());
  ASSERT_TRUE(extension);
  const ExtensionId extension_id = extension->id();

  EXPECT_TRUE(extension_prefs()->GetDisableReasons(extension_id).empty());
  EXPECT_TRUE(WasExtensionReEnabledByUser(extension_id));

  // The extension is reported as re-enabled by the user.
  histogram_tester().ExpectTotalCount(
      "Extensions.MV2Deprecation.MV2ExtensionState.Internal", 1);
  histogram_tester().ExpectBucketCount(
      "Extensions.MV2Deprecation.MV2ExtensionState.Internal",
      ManifestV2ExperimentManager::MV2ExtensionState::kUserReEnabled, 1);
}

// Tests that extensions are re-enabled automatically if they update to MV3.
IN_PROC_BROWSER_TEST_F(ManifestV2ExperimentManagerBrowserTest,
                       ExtensionsAreReEnabledWhenUpdatedToMV3) {
  EXPECT_EQ(MV2ExperimentStage::kDisableWithReEnable,
            GetActiveExperimentStage());

  WaitForExtensionSystemReady();

  static constexpr char kManifestTemplate[] =
      R"({
           "name": "Test Extension",
           "manifest_version": %d,
           "version": "%s"
         })";
  std::string mv2_manifest = base::StringPrintf(kManifestTemplate, 2, "1.0");
  std::string mv3_manifest = base::StringPrintf(kManifestTemplate, 3, "2.0");

  TestExtensionDir test_dir;
  test_dir.WriteManifest(mv2_manifest);
  base::FilePath mv2_crx = test_dir.Pack("mv2.crx");
  test_dir.WriteManifest(mv3_manifest);
  base::FilePath mv3_crx = test_dir.Pack("mv3.crx");

  const Extension* extension = InstallExtension(
      mv2_crx, /*expected_change=*/1, mojom::ManifestLocation::kInternal);
  ASSERT_TRUE(extension);
  const ExtensionId extension_id = extension->id();

  // Technically, this could be accomplished using a PRE_ test, similar to
  // other browser tests in this file. However, that makes it much more
  // difficult to update the extension to an MV3 version, since we couldn't
  // construct the extension dynamically.
  experiment_manager()->DisableAffectedExtensionsForTesting();

  // The MV2 extension is disabled.
  EXPECT_TRUE(
      extension_registry()->disabled_extensions().Contains(extension_id));
  EXPECT_THAT(extension_prefs()->GetDisableReasons(extension_id),
              testing::UnorderedElementsAre(
                  disable_reason::DISABLE_UNSUPPORTED_MANIFEST_VERSION));

  // Update the extension to MV3. Note: Even though this doesn't result in a
  // _new_ extension, the `expected_change` is 1 here because this results in
  // the extension being added to the enabled set (so the enabled extension
  // count is 1 higher than it was before).
  const Extension* updated_extension = UpdateExtension(extension_id, mv3_crx,
                                                       /*expected_change=*/1);
  ASSERT_TRUE(updated_extension);
  EXPECT_EQ(updated_extension->id(), extension_id);

  // The new MV3 extension should be enabled.
  EXPECT_EQ(3, updated_extension->manifest_version());
  EXPECT_TRUE(
      extension_registry()->enabled_extensions().Contains(extension_id));
  EXPECT_TRUE(extension_prefs()->GetDisableReasons(extension_id).empty());
  // The user didn't re-enable the extension, so it shouldn't be marked as such.
  EXPECT_FALSE(WasExtensionReEnabledByUser(extension_id));
}

// Tests that the MV2 deprecation notice for an extension is only acknowledged
// for the current stage.
// Step 1 (Warning Stage): Mark an extension's notice as acknowledged on this
// stage.
IN_PROC_BROWSER_TEST_F(ManifestV2ExperimentManagerBrowserTest,
                       PRE_MarkingNoticeAsAcknowledged) {
  EXPECT_EQ(MV2ExperimentStage::kWarning, GetActiveExperimentStage());

  WaitForExtensionSystemReady();

  // Add an extension and verify it's notice is not marked as acknowledged on
  // this stage.
  const Extension* extension = AddMV2Extension("Test MV2 Extension");
  ASSERT_TRUE(extension);
  EXPECT_FALSE(experiment_manager()->DidUserAcknowledgeNotice(extension->id()));

  // Mark the notice as acknowledged for this stage. Verify it's acknowledged.
  experiment_manager()->MarkNoticeAsAcknowledged(extension->id());
  EXPECT_TRUE(experiment_manager()->DidUserAcknowledgeNotice(extension->id()));
}
// Step 2 (Disable Stage): Verify extension's notice is not acknowledged on this
// stage. Mark notice as acknowledged on this stage.
IN_PROC_BROWSER_TEST_F(ManifestV2ExperimentManagerBrowserTest,
                       MarkingNoticeAsAcknowledged) {
  EXPECT_EQ(MV2ExperimentStage::kDisableWithReEnable,
            GetActiveExperimentStage());

  WaitForExtensionSystemReady();

  // Verify extension's notice is not marked as acknowledged on this stage, even
  // if it was acknowledged on the previous stage.
  const Extension* extension = GetExtensionByName(
      "Test MV2 Extension", extension_registry()->disabled_extensions());
  ASSERT_TRUE(extension);
  EXPECT_FALSE(experiment_manager()->DidUserAcknowledgeNotice(extension->id()));

  // Mark the notice as acknowledged for this stage. Verify it's acknowledged.
  experiment_manager()->MarkNoticeAsAcknowledged(extension->id());
  EXPECT_TRUE(experiment_manager()->DidUserAcknowledgeNotice(extension->id()));
}

// Tests that the MV2 deprecation global notice is only acknowledged for the
// current stage.
// Step 1 (Warning Stage): Mark the global notice as acknowledged
// on this stage.
IN_PROC_BROWSER_TEST_F(ManifestV2ExperimentManagerBrowserTest,
                       PRE_MarkingGlobalNoticeAsAcknowledged) {
  EXPECT_EQ(MV2ExperimentStage::kWarning, GetActiveExperimentStage());

  WaitForExtensionSystemReady();

  // Add an extension that should make the MV2 deprecation notice visible.
  // Verify global notice is not marked as acknowledged on this stage.
  const Extension* extension = AddMV2Extension("Test MV2 Extension");
  ASSERT_TRUE(extension);
  EXPECT_FALSE(experiment_manager()->DidUserAcknowledgeNoticeGlobally());

  // Mark the global notice as acknowledged for this stage. Verify it's
  // acknowledged.
  experiment_manager()->MarkNoticeAsAcknowledgedGlobally();
  EXPECT_TRUE(experiment_manager()->DidUserAcknowledgeNoticeGlobally());
}
// Step 2 (Disable Stage): Verify global notice is not acknowledged on this
// stage. Mark notice as acknowledged on this stage.
IN_PROC_BROWSER_TEST_F(ManifestV2ExperimentManagerBrowserTest,
                       MarkingGlobalNoticeAsAcknowledged) {
  EXPECT_EQ(MV2ExperimentStage::kDisableWithReEnable,
            GetActiveExperimentStage());

  WaitForExtensionSystemReady();

  // Verify global notice is not marked as acknowledged on this stage, even if
  // it was acknowledged on the previous stage.
  const Extension* extension = GetExtensionByName(
      "Test MV2 Extension", extension_registry()->disabled_extensions());
  ASSERT_TRUE(extension);
  EXPECT_FALSE(experiment_manager()->DidUserAcknowledgeNoticeGlobally());

  // Mark the global notice as acknowledged for this stage. Verify it's
  // acknowledged.
  experiment_manager()->MarkNoticeAsAcknowledgedGlobally();
  EXPECT_TRUE(experiment_manager()->DidUserAcknowledgeNoticeGlobally());
}

// Tests that if a user moves from a later experiment stage (disable with
// re-enable) to an earlier one (warning), any disabled extensions will be
// automatically re-enabled.
// First stage: install an MV2 extension.
IN_PROC_BROWSER_TEST_F(ManifestV2ExperimentManagerBrowserTest,
                       PRE_PRE_ExtensionsAreReEnabledIfExperimentDisabled) {
  EXPECT_EQ(MV2ExperimentStage::kWarning, GetActiveExperimentStage());

  const Extension* extension = AddMV2Extension("Test MV2 Extension");
  ASSERT_TRUE(extension);
}
// Second stage: MV2 deprecation experiment takes effect; extension is disabled.
IN_PROC_BROWSER_TEST_F(ManifestV2ExperimentManagerBrowserTest,
                       PRE_ExtensionsAreReEnabledIfExperimentDisabled) {
  EXPECT_EQ(MV2ExperimentStage::kDisableWithReEnable,
            GetActiveExperimentStage());

  WaitForExtensionSystemReady();

  const Extension* extension = GetExtensionByName(
      "Test MV2 Extension", extension_registry()->disabled_extensions());
  ASSERT_TRUE(extension);
  const ExtensionId extension_id = extension->id();
  EXPECT_THAT(extension_prefs()->GetDisableReasons(extension_id),
              testing::UnorderedElementsAre(
                  disable_reason::DISABLE_UNSUPPORTED_MANIFEST_VERSION));
}
// Third stage: Move the user back to the warning stage. The extension should be
// re-enabled.
IN_PROC_BROWSER_TEST_F(ManifestV2ExperimentManagerBrowserTest,
                       ExtensionsAreReEnabledIfExperimentDisabled) {
  EXPECT_EQ(MV2ExperimentStage::kWarning, GetActiveExperimentStage());

  WaitForExtensionSystemReady();

  const Extension* extension = GetExtensionByName(
      "Test MV2 Extension", extension_registry()->enabled_extensions());
  ASSERT_TRUE(extension);
  const ExtensionId extension_id = extension->id();

  EXPECT_TRUE(extension_prefs()->GetDisableReasons(extension_id).empty());
  // The user didn't re-enable the extension, so it shouldn't be marked as such.
  EXPECT_FALSE(WasExtensionReEnabledByUser(extension_id));

  // Since the user is no longer in the disable phase, no metrics should be
  // reported.
  histogram_tester().ExpectTotalCount(
      "Extensions.MV2Deprecation.MV2ExtensionState.Internal", 0);
}

// Tests that externally-installed extensions are allowed to be installed, but
// will still be disabled by the MV2 experiments.
IN_PROC_BROWSER_TEST_F(ManifestV2ExperimentManagerBrowserTest,
                       ExternalExtensionsCanBeInstalledButAreAlsoDisabled) {
  // External extensions are default-disabled on Windows and Mac. This won't
  // be affected by the MV2 deprecation, but for consistency of testing, we
  // disable this prompting in the test.
  FeatureSwitch::ScopedOverride feature_override(
      FeatureSwitch::prompt_for_external_extensions(), false);

  // TODO(devlin): Update this to a different extension so we use one dedicated
  // to this test ("good.crx" should likely be updated to MV3).
  static constexpr char kExtensionId[] = "ldnnhddmnhbkjipkidpdiheffobcpfmf";
  base::FilePath crx_path = test_data_dir_.AppendASCII("good.crx");

  // Install a new external extension.
  ExternalProviderManager* external_provider_manager =
      ExternalProviderManager::Get(profile());
  TestExtensionRegistryObserver observer(extension_registry());
  auto provider = std::make_unique<MockExternalProvider>(
      external_provider_manager, mojom::ManifestLocation::kExternalPref);
  provider->UpdateOrAddExtension(kExtensionId, "1.0.0.0", crx_path);
  external_provider_manager->AddProviderForTesting(std::move(provider));
  external_provider_manager->CheckForExternalUpdates();

  auto extension = observer.WaitForExtensionInstalled();
  EXPECT_EQ(extension->id(), kExtensionId);

  // The extension should install and be enabled. We allow installation of
  // external extensions (unlike webstore extensions) because we can't know if
  // the extension is MV2 or MV3 until we install it.
  // We could theoretically disable it immediately if it's MV2, but it'll get
  // disabled on the next run of Chrome.
  EXPECT_TRUE(
      extension_registry()->enabled_extensions().Contains(kExtensionId));
  EXPECT_TRUE(extension_prefs()->GetDisableReasons(kExtensionId).empty());

  // The extension should still be counted as "affected" by the MV2 deprecation.
  EXPECT_TRUE(experiment_manager()->IsExtensionAffected(*extension));

  // And should also be disabled when we check again.
  experiment_manager()->DisableAffectedExtensionsForTesting();
  EXPECT_TRUE(
      extension_registry()->disabled_extensions().Contains(kExtensionId));
  EXPECT_THAT(extension_prefs()->GetDisableReasons(kExtensionId),
              testing::UnorderedElementsAre(
                  disable_reason::DISABLE_UNSUPPORTED_MANIFEST_VERSION));
}

// Tests that a UKM event is emitted when the user uninstalls a disabled
// extension.
IN_PROC_BROWSER_TEST_F(ManifestV2ExperimentManagerBrowserTest,
                       UkmIsEmittedForExtensionWhenUninstalled) {
  EXPECT_EQ(MV2ExperimentStage::kDisableWithReEnable,
            GetActiveExperimentStage());

  WaitForExtensionSystemReady();

  const Extension* extension = AddMV2Extension("Test MV2 Extension");
  ASSERT_TRUE(extension);

  experiment_manager()->DisableAffectedExtensionsForTesting();

  EXPECT_TRUE(GetUkmEntries().empty());

  // Since the extension will be uninstalled (and the pointer will become unsafe
  // to use), cache its URL.
  const GURL extension_url = extension->url();
  UninstallExtension(extension->id(),
                     UninstallReason::UNINSTALL_REASON_USER_INITIATED);

  auto entries = GetUkmEntries();
  ASSERT_EQ(1u, entries.size());
  auto* entry = entries.front().get();
  ukm_recorder().ExpectEntrySourceHasUrl(entry, extension_url);
  ukm_recorder().ExpectEntryMetric(
      entry,
      ukm::builders::Extensions_MV2ExtensionHandledInSoftDisable::kActionName,
      static_cast<int64_t>(ManifestV2ExperimentManager::
                               ExtensionMV2DeprecationAction::kRemoved));
}

// Tests that UKM events are not emitted for unrelated uninstallations.
IN_PROC_BROWSER_TEST_F(ManifestV2ExperimentManagerBrowserTest,
                       UkmIsNotEmittedForOtherUninstallations) {
  EXPECT_EQ(MV2ExperimentStage::kDisableWithReEnable,
            GetActiveExperimentStage());

  WaitForExtensionSystemReady();

  const Extension* mv2_extension = AddMV2Extension("Test MV2 Extension");
  ASSERT_TRUE(mv2_extension);
  const Extension* mv3_extension =
      AddExtensionWithManifestVersion("Test MV3 Extension", 3);
  ASSERT_TRUE(mv3_extension);

  experiment_manager()->DisableAffectedExtensionsForTesting();

  EXPECT_TRUE(GetUkmEntries().empty());

  // Uninstalling an MV2 extension for a reason other than user uninstallation
  // should not trigger a UKM event.
  UninstallExtension(mv2_extension->id(),
                     UninstallReason::UNINSTALL_REASON_MANAGEMENT_API);
  EXPECT_TRUE(GetUkmEntries().empty());

  // Uninstalling extensions that aren't affected by the MV2 experiments should
  // not trigger a UKM event.
  UninstallExtension(mv3_extension->id(),
                     UninstallReason::UNINSTALL_REASON_USER_INITIATED);
  EXPECT_TRUE(GetUkmEntries().empty());
}

// Tests the flow from the "warning" phase to the "unsupported" phase.
IN_PROC_BROWSER_TEST_F(ManifestV2ExperimentManagerBrowserTest,
                       PRE_PRE_FlowFromWarningToUnsupported) {
  EXPECT_EQ(MV2ExperimentStage::kWarning, GetActiveExperimentStage());
  // Install two MV2 extensions.
  const Extension* extension1 = AddMV2Extension("Test MV2 Extension 1");
  ASSERT_TRUE(extension1);

  const Extension* extension2 = AddMV2Extension("Test MV2 Extension 2");
  ASSERT_TRUE(extension2);
}
IN_PROC_BROWSER_TEST_F(ManifestV2ExperimentManagerBrowserTest,
                       PRE_FlowFromWarningToUnsupported) {
  EXPECT_EQ(MV2ExperimentStage::kDisableWithReEnable,
            GetActiveExperimentStage());

  WaitForExtensionSystemReady();

  // Both extensions should be disabled.
  const Extension* extension1 = GetExtensionByName(
      "Test MV2 Extension 1", extension_registry()->disabled_extensions());
  ASSERT_TRUE(extension1);
  const ExtensionId extension_id1 = extension1->id();
  EXPECT_THAT(extension_prefs()->GetDisableReasons(extension_id1),
              testing::UnorderedElementsAre(
                  disable_reason::DISABLE_UNSUPPORTED_MANIFEST_VERSION));
  const Extension* extension2 = GetExtensionByName(
      "Test MV2 Extension 2", extension_registry()->disabled_extensions());
  ASSERT_TRUE(extension1);
  const ExtensionId extension_id2 = extension2->id();
  EXPECT_THAT(extension_prefs()->GetDisableReasons(extension_id2),
              testing::UnorderedElementsAre(
                  disable_reason::DISABLE_UNSUPPORTED_MANIFEST_VERSION));

  // The extensions should be recorded as "soft disabled".
  histogram_tester().ExpectTotalCount(
      "Extensions.MV2Deprecation.MV2ExtensionState.Internal", 2);
  histogram_tester().ExpectBucketCount(
      "Extensions.MV2Deprecation.MV2ExtensionState.Internal",
      ManifestV2ExperimentManager::MV2ExtensionState::kSoftDisabled, 2);

  // The user should be allowed to re-enable the extensions.
  ExtensionSystem* system = ExtensionSystem::Get(profile());
  {
    disable_reason::DisableReason disable_reason = disable_reason::DISABLE_NONE;
    EXPECT_FALSE(system->management_policy()->MustRemainDisabled(
        extension1, &disable_reason));
    EXPECT_EQ(disable_reason::DISABLE_NONE, disable_reason);
  }
  {
    disable_reason::DisableReason disable_reason = disable_reason::DISABLE_NONE;
    EXPECT_FALSE(system->management_policy()->MustRemainDisabled(
        extension2, &disable_reason));
    EXPECT_EQ(disable_reason::DISABLE_NONE, disable_reason);
  }

  // Re-enable the first MV2 extension (this is allowed in this phase).
  extension_registrar()->EnableExtension(extension_id1);

  // The first extension should be properly re-enabled, the disable reasons
  // cleared, and the extension should be marked as explicitly re-enabled.
  EXPECT_TRUE(
      extension_registry()->enabled_extensions().Contains(extension_id1));
  EXPECT_TRUE(extension_prefs()->GetDisableReasons(extension_id1).empty());
  EXPECT_TRUE(WasExtensionReEnabledByUser(extension_id1));
}
IN_PROC_BROWSER_TEST_F(ManifestV2ExperimentManagerBrowserTest,
                       FlowFromWarningToUnsupported) {
  EXPECT_EQ(MV2ExperimentStage::kUnsupported, GetActiveExperimentStage());

  WaitForExtensionSystemReady();

  // Both extensions should be disabled.
  // In the "unsupported" phase, both extensions should be disabled again, even
  // though the first was re-enabled in a previous phase.
  const Extension* extension1 = GetExtensionByName(
      "Test MV2 Extension 1", extension_registry()->disabled_extensions());
  ASSERT_TRUE(extension1);
  const ExtensionId extension_id1 = extension1->id();

  EXPECT_TRUE(WasExtensionReEnabledByUser(extension_id1));
  EXPECT_THAT(extension_prefs()->GetDisableReasons(extension_id1),
              testing::UnorderedElementsAre(
                  disable_reason::DISABLE_UNSUPPORTED_MANIFEST_VERSION));

  const Extension* extension2 = GetExtensionByName(
      "Test MV2 Extension 2", extension_registry()->disabled_extensions());
  ASSERT_TRUE(extension2);
  const ExtensionId extension_id2 = extension2->id();

  EXPECT_FALSE(WasExtensionReEnabledByUser(extension_id2));
  EXPECT_THAT(extension_prefs()->GetDisableReasons(extension_id2),
              testing::UnorderedElementsAre(
                  disable_reason::DISABLE_UNSUPPORTED_MANIFEST_VERSION));

  // The extensions should now be recorded as "hard disabled".
  histogram_tester().ExpectTotalCount(
      "Extensions.MV2Deprecation.MV2ExtensionState.Internal", 2);
  histogram_tester().ExpectBucketCount(
      "Extensions.MV2Deprecation.MV2ExtensionState.Internal",
      ManifestV2ExperimentManager::MV2ExtensionState::kHardDisabled, 2);

  // The user should no longer be allowed to re-enable the extensions.
  ExtensionSystem* system = ExtensionSystem::Get(profile());
  {
    disable_reason::DisableReason disable_reason = disable_reason::DISABLE_NONE;
    EXPECT_TRUE(system->management_policy()->MustRemainDisabled(
        extension1, &disable_reason));
    EXPECT_EQ(disable_reason::DISABLE_UNSUPPORTED_MANIFEST_VERSION,
              disable_reason);
  }
  {
    disable_reason::DisableReason disable_reason = disable_reason::DISABLE_NONE;
    EXPECT_TRUE(system->management_policy()->MustRemainDisabled(
        extension2, &disable_reason));
    EXPECT_EQ(disable_reason::DISABLE_UNSUPPORTED_MANIFEST_VERSION,
              disable_reason);
  }
}

// Tests that unpacked extensions can be installed in the disabled experiment
// phase.
IN_PROC_BROWSER_TEST_F(ManifestV2ExperimentManagerBrowserTest,
                       UnpackedExtensionsCanBeInstalledInDisabledPhase) {
  EXPECT_EQ(MV2ExperimentStage::kDisableWithReEnable,
            GetActiveExperimentStage());
  WaitForExtensionSystemReady();

  static constexpr char kMv2Manifest[] =
      R"({
           "name": "Simple MV2",
           "manifest_version": 2,
           "version": "0.1"
         })";
  TestExtensionDir test_dir;
  test_dir.WriteManifest(kMv2Manifest);

  base::RunLoop run_loop;
  std::string id;
  scoped_refptr<UnpackedInstaller> installer =
      UnpackedInstaller::Create(profile());
  auto on_complete = [&run_loop, &id](const Extension* extension,
                                      const base::FilePath& file_path,
                                      const std::string& error) {
    EXPECT_TRUE(extension);
    EXPECT_EQ("", error);
    id = extension->id();
    run_loop.Quit();
  };
  installer->set_completion_callback(base::BindLambdaForTesting(on_complete));
  installer->set_be_noisy_on_failure(false);
  installer->Load(test_dir.UnpackedPath());
  run_loop.Run();

  EXPECT_TRUE(extension_registry()->enabled_extensions().Contains(id));
}

// Tests that unpacked extensions cannot be installed in the unsupported
// experiment phase.
IN_PROC_BROWSER_TEST_F(ManifestV2ExperimentManagerBrowserTest,
                       UnpackedExtensionsCannotBeInstalledInUnsupportedPhase) {
  EXPECT_EQ(MV2ExperimentStage::kUnsupported, GetActiveExperimentStage());
  WaitForExtensionSystemReady();

  static constexpr char kMv2Manifest[] =
      R"({
           "name": "Simple MV2",
           "manifest_version": 2,
           "version": "0.1"
         })";
  TestExtensionDir test_dir;
  test_dir.WriteManifest(kMv2Manifest);

  base::RunLoop run_loop;
  std::string install_error;
  scoped_refptr<UnpackedInstaller> installer =
      UnpackedInstaller::Create(profile());
  auto on_complete = [&run_loop, &install_error](
                         const Extension* extension,
                         const base::FilePath& file_path,
                         const std::string& error) {
    install_error = error;
    run_loop.Quit();
  };
  installer->set_completion_callback(base::BindLambdaForTesting(on_complete));
  installer->set_be_noisy_on_failure(false);
  installer->Load(test_dir.UnpackedPath());
  run_loop.Run();

  EXPECT_EQ(
      "Cannot install extension because it uses an unsupported "
      "manifest version.",
      install_error);
}

class ManifestV2ExperimentWithLegacyExtensionSupportTest
    : public ManifestV2ExperimentManagerBrowserTest {
 public:
  ManifestV2ExperimentWithLegacyExtensionSupportTest() = default;
  ~ManifestV2ExperimentWithLegacyExtensionSupportTest() override = default;

 private:
  void PopulateAdditionalFeatures(
      std::vector<base::test::FeatureRef>& enabled_features,
      std::vector<base::test::FeatureRef>& disabled_features) override {
    enabled_features.push_back(extensions_features::kAllowLegacyMV2Extensions);
  }
};

// Tests that legacy unpacked MV2 extensions are still allowed (and aren't
// auto-disabled) if the kAllowLegacyMV2Extensions feature is enabled.
IN_PROC_BROWSER_TEST_F(
    ManifestV2ExperimentWithLegacyExtensionSupportTest,
    PRE_MV2ExtensionsAreNotDisabledIfLegacyExtensionSwitchIsApplied) {
  EXPECT_EQ(MV2ExperimentStage::kWarning, GetActiveExperimentStage());

  // Load two extensions: a packed extension and an unpacked extension.
  const Extension* packed_extension =
      AddMV2Extension("Test Packed MV2 Extension");
  ASSERT_TRUE(packed_extension);
  EXPECT_EQ(mojom::ManifestLocation::kInternal, packed_extension->location());

  const Extension* unpacked_extension =
      LoadExtension(test_data_dir_.AppendASCII("simple_mv2"));
  ASSERT_TRUE(unpacked_extension);
  EXPECT_EQ(mojom::ManifestLocation::kUnpacked, unpacked_extension->location());
}
IN_PROC_BROWSER_TEST_F(
    ManifestV2ExperimentWithLegacyExtensionSupportTest,
    MV2ExtensionsAreNotDisabledIfLegacyExtensionSwitchIsApplied) {
  EXPECT_EQ(MV2ExperimentStage::kUnsupported, GetActiveExperimentStage());

  WaitForExtensionSystemReady();

  // The packed extension should have been disabled.
  const Extension* packed_extension = GetExtensionByName(
      "Test Packed MV2 Extension", extension_registry()->disabled_extensions());
  ASSERT_TRUE(packed_extension);
  const ExtensionId packed_extension_id = packed_extension->id();

  EXPECT_THAT(extension_prefs()->GetDisableReasons(packed_extension_id),
              testing::UnorderedElementsAre(
                  disable_reason::DISABLE_UNSUPPORTED_MANIFEST_VERSION));
  // The user didn't re-enable the extension, so it shouldn't be marked as such.
  EXPECT_FALSE(WasExtensionReEnabledByUser(packed_extension_id));

  // The user is not allowed to re-enable the packed extension; the flag only
  // applies to unpacked extensions.
  ExtensionSystem* system = ExtensionSystem::Get(profile());
  disable_reason::DisableReason disable_reason = disable_reason::DISABLE_NONE;
  EXPECT_TRUE(system->management_policy()->MustRemainDisabled(packed_extension,
                                                              &disable_reason));
  EXPECT_EQ(disable_reason::DISABLE_UNSUPPORTED_MANIFEST_VERSION,
            disable_reason);

  // The unpacked extension should still be enabled.
  const Extension* unpacked_extension = GetExtensionByName(
      "Simple MV2 Extension", extension_registry()->enabled_extensions());
  ASSERT_TRUE(unpacked_extension);
  const ExtensionId unpacked_extension_id = unpacked_extension->id();

  EXPECT_TRUE(
      extension_prefs()->GetDisableReasons(unpacked_extension_id).empty());
  // The user didn't re-enable the extension, so it shouldn't be marked as such.
  EXPECT_FALSE(WasExtensionReEnabledByUser(unpacked_extension_id));

  // The user is allowed to re-enable the unpacked extension.
  disable_reason = disable_reason::DISABLE_NONE;
  EXPECT_FALSE(system->management_policy()->MustRemainDisabled(
      unpacked_extension, &disable_reason));
  EXPECT_EQ(disable_reason::DISABLE_NONE, disable_reason);
}

}  // namespace extensions
