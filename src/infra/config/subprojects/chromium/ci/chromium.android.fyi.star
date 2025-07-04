# Copyright 2021 The Chromium Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Definitions of builders in the chromium.android.fyi builder group."""

load("//lib/builder_config.star", "builder_config")
load("//lib/builder_health_indicators.star", "health_spec")
load("//lib/builders.star", "os", "siso")
load("//lib/ci.star", "ci")
load("//lib/consoles.star", "consoles")
load("//lib/gn_args.star", "gn_args")
load("//lib/targets.star", "targets")

ci.defaults.set(
    executable = ci.DEFAULT_EXECUTABLE,
    builder_group = "chromium.android.fyi",
    pool = ci.DEFAULT_POOL,
    cores = 8,
    os = os.LINUX_DEFAULT,
    execution_timeout = ci.DEFAULT_EXECUTION_TIMEOUT,
    health_spec = health_spec.DEFAULT,
    priority = ci.DEFAULT_FYI_PRIORITY,
    service_account = ci.DEFAULT_SERVICE_ACCOUNT,
    shadow_service_account = ci.DEFAULT_SHADOW_SERVICE_ACCOUNT,
    siso_project = siso.project.DEFAULT_TRUSTED,
    siso_remote_jobs = siso.remote_jobs.DEFAULT,
)

targets.builder_defaults.set(
    mixins = ["chromium-tester-service-account"],
)

consoles.console_view(
    name = "chromium.android.fyi",
    ordering = {
        None: ["android", "memory", "webview"],
    },
)

ci.builder(
    name = "android-15-chrome-wpt-fyi-rel",
    description_html = "This builder runs upstream web platform tests for reporting results to wpt.fyi.",
    builder_spec = builder_config.builder_spec(
        gclient_config = builder_config.gclient_config(
            config = "chromium",
            apply_configs = ["android"],
        ),
        chromium_config = builder_config.chromium_config(
            config = "main_builder",
            apply_configs = ["mb"],
            build_config = builder_config.build_config.RELEASE,
            target_arch = builder_config.target_arch.INTEL,
            target_bits = 64,
            target_platform = builder_config.target_platform.ANDROID,
        ),
        android_config = builder_config.android_config(config = "base_config"),
        build_gs_bucket = "chromium-android-archive",
    ),
    gn_args = gn_args.config(
        configs = [
            "android_builder",
            "release_builder",
            "remoteexec",
            "minimal_symbols",
            "x64",
            "strip_debug_info",
            "android_fastbuild",
            "webview_trichrome",
            "webview_shell",
        ],
    ),
    targets = targets.bundle(
        targets = [
            "chrome_public_wpt_suite",
        ],
        mixins = [
            "15-x64-emulator",
            "emulator-8-cores",
            "has_native_resultdb_integration",
            "linux-jammy",
            "x86-64",
        ],
        per_test_modifications = {
            "chrome_public_wpt": targets.mixin(
                args = [
                    "--use-upstream-wpt",
                    "--timeout-multiplier=4",
                ],
            ),
        },
    ),
    targets_settings = targets.settings(
        os_type = targets.os_type.ANDROID,
    ),
    console_view_entry = consoles.console_view_entry(
        category = "wpt|chrome",
        short_name = "15",
    ),
    contact_team_email = "chrome-product-engprod@google.com",
    execution_timeout = 4 * time.hour,
)

ci.builder(
    name = "android-15-webview-wpt-fyi-rel",
    description_html = "This builder runs upstream web platform tests for reporting results to wpt.fyi.",
    builder_spec = builder_config.builder_spec(
        gclient_config = builder_config.gclient_config(
            config = "chromium",
            apply_configs = ["android"],
        ),
        chromium_config = builder_config.chromium_config(
            config = "main_builder",
            apply_configs = ["mb"],
            build_config = builder_config.build_config.RELEASE,
            target_arch = builder_config.target_arch.INTEL,
            target_bits = 64,
            target_platform = builder_config.target_platform.ANDROID,
        ),
        android_config = builder_config.android_config(config = "base_config"),
        build_gs_bucket = "chromium-android-archive",
    ),
    gn_args = gn_args.config(
        configs = [
            "android_builder",
            "release_builder",
            "remoteexec",
            "minimal_symbols",
            "x64",
            "strip_debug_info",
            "android_fastbuild",
            "webview_trichrome",
            "webview_shell",
        ],
    ),
    targets = targets.bundle(
        targets = [
            "android_webview_wpt_tests",
        ],
        mixins = [
            "15-x64-emulator",
            "emulator-8-cores",
            "has_native_resultdb_integration",
            "linux-jammy",
            "x86-64",
        ],
        per_test_modifications = {
            "android_webview_wpt_tests": targets.mixin(
                args = [
                    "--use-upstream-wpt",
                ],
                swarming = targets.swarming(
                    shards = 36,
                ),
            ),
        },
    ),
    targets_settings = targets.settings(
        os_type = targets.os_type.ANDROID,
    ),
    console_view_entry = consoles.console_view_entry(
        category = "wpt|webview",
        short_name = "15",
    ),
    contact_team_email = "chrome-product-engprod@google.com",
)

# TODO(crbug.com/1022533#c40): Remove this builder once there are no associated
# disabled tests.
ci.builder(
    name = "android-pie-x86-fyi-rel",
    # Set to an empty list to avoid chromium-gitiles-trigger triggering new
    # builds. Also we don't set any `schedule` since this builder is for
    # reference only and should not run any new builds.
    triggered_by = [],
    builder_spec = builder_config.builder_spec(
        gclient_config = builder_config.gclient_config(
            config = "chromium",
            apply_configs = [
                "android",
                # This is necessary due to this builder running the
                # telemetry_perf_unittests suite.
                "chromium_with_telemetry_dependencies",
                "enable_wpr_tests",
            ],
        ),
        chromium_config = builder_config.chromium_config(
            config = "main_builder",
            apply_configs = ["mb"],
            build_config = builder_config.build_config.RELEASE,
            target_arch = builder_config.target_arch.INTEL,
            target_bits = 32,
            target_platform = builder_config.target_platform.ANDROID,
        ),
        android_config = builder_config.android_config(config = "base_config"),
        build_gs_bucket = "chromium-android-archive",
    ),
    gn_args = gn_args.config(
        configs = [
            "android_builder",
            "release_builder",
            "remoteexec",
            "minimal_symbols",
            "x86",
            "strip_debug_info",
            "android_fastbuild",
            "webview_monochrome",
            "webview_shell",
        ],
    ),
    targets = targets.bundle(
        targets = [
            targets.bundle(
                targets = [
                    "android_pie_emulator_gtests",
                    "pie_isolated_scripts",
                ],
            ),
            "chromium_android_scripts",
        ],
        additional_compile_targets = [
            "chrome_nocompile_tests",
        ],
        mixins = [
            "has_native_resultdb_integration",
            "isolate_profile_data",
            "pie-x86-emulator",
            "emulator-4-cores",
            "linux-jammy",
            "x86-64",
        ],
        per_test_modifications = {
            "android_browsertests": targets.mixin(
                args = [
                    # https://crbug.com/1034001
                    "--gtest_filter=-ImportantSitesUtilBrowserTest.DSENotConsideredImportantInRegularMode",
                ],
                swarming = targets.swarming(
                    dimensions = {
                        # crbug.com/1292221
                        "cores": "8",
                    },
                    shards = 9,
                ),
            ),
            "android_sync_integration_tests": targets.mixin(
                swarming = targets.swarming(
                    shards = 2,
                ),
            ),
            "cc_unittests": targets.mixin(
                args = [
                    # https://crbug.com/1039860
                    "--test-launcher-filter-file=../../testing/buildbot/filters/android.emulator.cc_unittests.filter",
                ],
            ),
            "chrome_public_test_apk": targets.mixin(
                args = [
                    # https://crbug.com/1046059
                    "--test-launcher-filter-file=../../testing/buildbot/filters/android.emulator_p.chrome_public_test_apk.filter",
                ],
                swarming = targets.swarming(
                    dimensions = {
                        "cores": "8",
                    },
                    # See https://crbug.com/1230192, runs of 40-60 minutes at 20 shards.
                    shards = 75,
                ),
            ),
            "components_browsertests": targets.mixin(
                swarming = targets.swarming(
                    shards = 4,
                ),
            ),
            "content_browsertests": targets.mixin(
                args = [
                    "--test-launcher-filter-file=../../testing/buildbot/filters/android.emulator_p.content_browsertests.filter",
                ],
                swarming = targets.swarming(
                    dimensions = {
                        # use 8-core to shorten runtime
                        "cores": "8",
                    },
                    shards = 75,
                ),
            ),
            # If you change this, make similar changes in android-x86-code-coverage
            "content_shell_crash_test": targets.remove(
                reason = "crbug.com/1084353",
            ),
            "content_shell_test_apk": targets.mixin(
                args = [
                    "--gtest_filter=-org.chromium.content.browser.input.ImeInputModeTest.testShowAndHideInputMode*",
                ],
                swarming = targets.swarming(
                    dimensions = {
                        # use 8-core to shorten runtime
                        "cores": "8",
                    },
                    shards = 6,
                ),
            ),
            "gl_tests_validating": targets.mixin(
                args = [
                    "--test-launcher-filter-file=../../testing/buildbot/filters/android.emulator_o_p.gl_tests.filter",
                ],
            ),
            "net_unittests": targets.mixin(
                # crbug.com/1046060
                args = [
                    "--test-launcher-filter-file=../../testing/buildbot/filters/android.emulator.net_unittests.filter",
                ],
            ),
            "perfetto_unittests": targets.remove(
                reason = "TODO(crbug.com/41440830): Fix permission issue when creating tmp files",
            ),
            "services_unittests": targets.mixin(
                args = [
                    # TODO(crbug.com/40203477): Fix the failed tests
                    "--gtest_filter=-PacLibraryTest.ActualPacMyIpAddress*",
                ],
                swarming = targets.swarming(
                    shards = 3,
                ),
            ),
            "telemetry_perf_unittests_android_chrome": targets.mixin(
                # For whatever reason, automatic browser selection on this bot chooses
                # webview instead of the full browser, so explicitly specify it here.
                args = [
                    "--browser=android-chromium",
                ],
            ),
            "webview_instrumentation_test_apk_multiple_process_mode": targets.mixin(
                args = [
                    "--test-launcher-filter-file=../../testing/buildbot/filters/android.emulator.webview_instrumentation_test_apk.filter",
                ],
                swarming = targets.swarming(
                    # crbug.com/1294924
                    shards = 15,
                ),
            ),
            "webview_instrumentation_test_apk_single_process_mode": targets.mixin(
                args = [
                    "--test-launcher-filter-file=../../testing/buildbot/filters/android.emulator.webview_instrumentation_test_apk.filter",
                ],
                swarming = targets.swarming(
                    # crbug.com/1294924
                    shards = 9,
                ),
            ),
        },
    ),
    targets_settings = targets.settings(
        os_type = targets.os_type.ANDROID,
    ),
    console_view_entry = consoles.console_view_entry(
        category = "emulator|x86|rel",
        short_name = "P",
    ),
)

ci.builder(
    name = "android-10-x86-fyi-rel",
    description_html = "Run chromium tests on Android 10 emulators.",
    builder_spec = builder_config.builder_spec(
        gclient_config = builder_config.gclient_config(
            config = "chromium",
            apply_configs = [
                "android",
            ],
        ),
        chromium_config = builder_config.chromium_config(
            config = "main_builder",
            apply_configs = ["mb"],
            build_config = builder_config.build_config.RELEASE,
            target_arch = builder_config.target_arch.INTEL,
            target_bits = 32,
            target_platform = builder_config.target_platform.ANDROID,
        ),
        android_config = builder_config.android_config(config = "base_config"),
        build_gs_bucket = "chromium-android-archive",
    ),
    gn_args = gn_args.config(
        configs = [
            "android_builder",
            "release_builder",
            "remoteexec",
            "minimal_symbols",
            "x86",
            "strip_debug_info",
            "android_fastbuild",
            "webview_trichrome",
            "webview_shell",
        ],
    ),
    targets = targets.bundle(
        targets = [
            targets.bundle(
                targets = [
                    "android_10_emulator_fyi_gtests",
                ],
                mixins = targets.mixin(
                    args = [
                        "--use-persistent-shell",
                    ],
                ),
            ),
        ],
        additional_compile_targets = [
            "chrome_nocompile_tests",
        ],
        mixins = [
            "has_native_resultdb_integration",
            "isolate_profile_data",
            "10-x86-emulator",
            "emulator-4-cores",
            "linux-jammy",
            "x86-64",
        ],
        per_test_modifications = {
            "content_browsertests": targets.mixin(
                args = [
                    "--test-launcher-filter-file=../../testing/buildbot/filters/android.emulator_10.content_browsertests.filter",
                    "--emulator-debug-tags=all",
                ],
                swarming = targets.swarming(
                    shards = 40,
                ),
            ),
        },
    ),
    targets_settings = targets.settings(
        os_type = targets.os_type.ANDROID,
    ),
    console_view_entry = consoles.console_view_entry(
        category = "emulator|x86|rel",
        short_name = "10",
    ),
    contact_team_email = "clank-engprod@google.com",
)

# TODO(crbug.com/40152686): This and android-12-x64-fyi-rel
# are being kept around so that build links in the related
# bugs are accessible
# Remove these once the bugs are closed
ci.builder(
    name = "android-11-x86-fyi-rel",
    builder_spec = builder_config.builder_spec(
        gclient_config = builder_config.gclient_config(
            config = "chromium",
            apply_configs = ["android"],
        ),
        chromium_config = builder_config.chromium_config(
            config = "main_builder",
            apply_configs = ["mb"],
            build_config = builder_config.build_config.RELEASE,
            target_arch = builder_config.target_arch.INTEL,
            target_bits = 32,
            target_platform = builder_config.target_platform.ANDROID,
        ),
        android_config = builder_config.android_config(config = "base_config"),
        build_gs_bucket = "chromium-android-archive",
    ),
    gn_args = gn_args.config(
        configs = [
            "android_builder",
            "release_builder",
            "remoteexec",
            "minimal_symbols",
            "x86",
            "strip_debug_info",
            "android_fastbuild",
            "webview_trichrome",
            "webview_shell",
        ],
    ),
    targets = targets.bundle(
        targets = [
            "android_11_emulator_fyi_gtests",
        ],
        mixins = [
            "11-x86-emulator",
            "emulator-4-cores",
            "has_native_resultdb_integration",
            "linux-jammy",
            "x86-64",
        ],
        per_test_modifications = {
            "chrome_public_test_apk": targets.mixin(
                args = [
                    "--test-launcher-filter-file=../../testing/buildbot/filters/android.emulator_11.chrome_public_test_apk.filter",
                    "--timeout-scale=2.0",
                ],
                # TODO(crbug.com/40210655) Move to CI builder once stable
                swarming = targets.swarming(
                    dimensions = {
                        "cores": "8",
                    },
                ),
            ),
            "content_browsertests": targets.mixin(
                # TODO(crbug.com/40210655) Move to CI builder once stable
                swarming = targets.swarming(
                    dimensions = {
                        # use 8-core to shorten runtime
                        "cores": "8",
                    },
                    shards = 30,
                ),
            ),
        },
    ),
    targets_settings = targets.settings(
        os_type = targets.os_type.ANDROID,
    ),
    console_view_entry = consoles.console_view_entry(
        category = "emulator|x86|rel",
        short_name = "11",
    ),
)

ci.builder(
    name = "android-12-x64-fyi-rel",
    parent = "ci/android-12-x64-rel",
    builder_spec = builder_config.builder_spec(
        execution_mode = builder_config.execution_mode.TEST,
        gclient_config = builder_config.gclient_config(
            config = "chromium",
            apply_configs = [
                "android",
            ],
        ),
        chromium_config = builder_config.chromium_config(
            config = "main_builder",
            apply_configs = [
                "mb",
            ],
            build_config = builder_config.build_config.RELEASE,
            target_arch = builder_config.target_arch.INTEL,
            target_bits = 64,
            target_platform = builder_config.target_platform.ANDROID,
        ),
        android_config = builder_config.android_config(
            config = "base_config",
        ),
        build_gs_bucket = "chromium-android-archive",
    ),
    targets = targets.bundle(
        targets = [
            "android_12_emulator_gtests",
        ],
        mixins = [
            "12-google-atd-x64-emulator",
            "emulator-8-cores",
            "has_native_resultdb_integration",
            "linux-jammy",
            "x86-64",
        ],
    ),
    targets_settings = targets.settings(
        os_type = targets.os_type.ANDROID,
    ),
    console_view_entry = consoles.console_view_entry(
        category = "emulator|x64|rel",
        short_name = "12",
    ),
    # Android x64 builds take longer than x86 builds to compile
    # So they need longer timeouts
    # Matching the execution time out of the android-12-x64-rel
    execution_timeout = 4 * time.hour,
)

# TODO(crbug.com/40263601): Remove after experimental is done.
ci.builder(
    name = "android-12l-x64-fyi-dbg",
    builder_spec = builder_config.builder_spec(
        gclient_config = builder_config.gclient_config(
            config = "chromium",
            apply_configs = [
                "android",
            ],
        ),
        chromium_config = builder_config.chromium_config(
            config = "main_builder",
            apply_configs = [
                "mb",
                "download_xr_test_apks",
            ],
            build_config = builder_config.build_config.DEBUG,
            target_arch = builder_config.target_arch.INTEL,
            target_bits = 64,
            target_platform = builder_config.target_platform.ANDROID,
        ),
        android_config = builder_config.android_config(
            config = "base_config",
        ),
        build_gs_bucket = "chromium-android-archive",
    ),
    gn_args = gn_args.config(
        configs = [
            "android_builder",
            "android_with_static_analysis",
            "debug_static_builder",
            "remoteexec",
            "x64",
            "webview_trichrome",
            "webview_shell",
        ],
    ),
    targets = targets.bundle(
        targets = [
            "android_lff_emulator_gtests",
        ],
        mixins = [
            "12l-fyi-x64-emulator",
            "emulator-8-cores",
            "has_native_resultdb_integration",
            "linux-jammy",
            "x86-64",
        ],
        per_test_modifications = {
            "android_browsertests": targets.mixin(
                args = [
                    # https://crbug.com/1289764
                    "--gtest_filter=-All/ChromeBrowsingDataLifetimeManagerScheduledRemovalTest.History/*",
                ],
            ),
            "base_unittests": targets.mixin(
                args = [
                    "--test-launcher-filter-file=../../testing/buildbot/filters/android.emulator.base_unittests.filter",
                ],
            ),
            "chrome_public_test_apk": targets.mixin(
                args = [
                    "--test-launcher-filter-file=../../testing/buildbot/filters/android.emulator_12l.chrome_public_test_apk.filter",
                ],
            ),
            "chrome_public_unit_test_apk": targets.mixin(
                args = [
                    "--test-launcher-filter-file=../../testing/buildbot/filters/android.emulator_12l.chrome_public_unit_test_apk.filter",
                ],
            ),
            "content_browsertests": targets.mixin(
                args = [
                    "--test-launcher-filter-file=../../testing/buildbot/filters/android.emulator_12l.content_browsertests.filter",
                ],
                swarming = targets.swarming(
                    shards = 40,
                ),
            ),
            "content_shell_test_apk": targets.mixin(
                args = [
                    "--test-launcher-filter-file=../../testing/buildbot/filters/android.emulator_12l.content_shell_test_apk.filter",
                ],
            ),
            "crashpad_tests": targets.mixin(
                args = [
                    "--test-launcher-filter-file=../../testing/buildbot/filters/android.emulator.crashpad_tests.filter",
                ],
            ),
            "device_unittests": targets.mixin(
                args = [
                    "--test-launcher-filter-file=../../testing/buildbot/filters/android.emulator.device_unittests.filter",
                ],
            ),
            "gl_tests_validating": targets.mixin(
                args = [
                    "--test-launcher-filter-file=../../testing/buildbot/filters/android.emulator_12_12l_13.gl_tests.filter",
                ],
            ),
            "media_unittests": targets.mixin(
                args = [
                    "--test-launcher-filter-file=../../testing/buildbot/filters/android.emulator.media_unittests.filter",
                ],
            ),
            "perfetto_unittests": targets.mixin(
                args = [
                    # TODO(crbug.com/40201873): Fix the failed test
                    "--gtest_filter=-ScopedDirTest.CloseOutOfScope",
                ],
            ),
            "webview_instrumentation_test_apk_multiple_process_mode": targets.mixin(
                swarming = targets.swarming(
                    shards = 9,
                ),
            ),
        },
    ),
    targets_settings = targets.settings(
        os_type = targets.os_type.ANDROID,
    ),
    console_view_entry = consoles.console_view_entry(
        category = "emulator|x64|dbg",
        short_name = "12L",
    ),
    # Android x64 builds take longer than x86 builds to compile
    # So they need longer timeouts
    # Matching the execution time out of the android-12-x64-rel
    execution_timeout = 4 * time.hour,
)

ci.builder(
    name = "android-13-x64-fyi-rel",
    description_html = "Run chromium tests on Android 13 emulators for experimental.",
    # Set to trigger manually as there is no experiment at the moment.
    triggered_by = [],
    builder_spec = builder_config.builder_spec(
        gclient_config = builder_config.gclient_config(
            config = "chromium",
            apply_configs = [
                "android",
            ],
        ),
        chromium_config = builder_config.chromium_config(
            config = "main_builder",
            apply_configs = ["mb"],
            build_config = builder_config.build_config.RELEASE,
            target_arch = builder_config.target_arch.INTEL,
            target_bits = 64,
            target_platform = builder_config.target_platform.ANDROID,
        ),
        android_config = builder_config.android_config(
            config = "base_config",
        ),
        build_gs_bucket = "chromium-android-archive",
    ),
    gn_args = gn_args.config(
        configs = [
            "android_builder",
            "release_builder",
            "remoteexec",
            "minimal_symbols",
            "x64",
            "strip_debug_info",
            "android_fastbuild",
            "webview_trichrome",
            "webview_shell",
        ],
    ),
    targets = targets.bundle(
        targets = [
            "android_13_emulator_gtests",
        ],
        mixins = [
            "13-x64-emulator",
            "emulator-8-cores",
            "has_native_resultdb_integration",
            "linux-jammy",
            "x86-64",
        ],
        per_test_modifications = {
            "android_browsertests": targets.mixin(
                args = [
                    # https://crbug.com/1414886
                    "--gtest_filter=-OfferNotificationControllerAndroidBrowserTestForMessagesUi.MessageShown",
                ],
                ci_only = True,
            ),
            "android_sync_integration_tests": targets.mixin(
                swarming = targets.swarming(
                    shards = 2,
                ),
            ),
            "base_unittests": targets.mixin(
                args = [
                    "--test-launcher-filter-file=../../testing/buildbot/filters/android.emulator.base_unittests.filter",
                ],
            ),
            "chrome_public_test_apk": targets.mixin(
                args = [
                    "--test-launcher-filter-file=../../testing/buildbot/filters/android.emulator_13.chrome_public_test_apk.filter",
                ],
                swarming = targets.swarming(
                    shards = 40,
                ),
            ),
            "chrome_public_unit_test_apk": targets.mixin(
                args = [
                    "--test-launcher-filter-file=../../testing/buildbot/filters/android.emulator_13.chrome_public_unit_test_apk.filter",
                ],
            ),
            "content_browsertests": targets.mixin(
                args = [
                    "--test-launcher-filter-file=../../testing/buildbot/filters/android.emulator_13.content_browsertests.filter",
                ],
                ci_only = True,
                swarming = targets.swarming(
                    shards = 40,
                ),
            ),
            "content_shell_test_apk": targets.mixin(
                args = [
                    "--test-launcher-filter-file=../../testing/buildbot/filters/android.emulator_13.content_shell_test_apk.filter",
                ],
                ci_only = True,
            ),
            "crashpad_tests": targets.mixin(
                args = [
                    "--test-launcher-filter-file=../../testing/buildbot/filters/android.emulator.crashpad_tests.filter",
                ],
                # TODO(crbug.com/337935399): Remove experiment after the bug is fixed.
                experiment_percentage = 100,
            ),
            "device_unittests": targets.mixin(
                args = [
                    "--test-launcher-filter-file=../../testing/buildbot/filters/android.emulator.device_unittests.filter",
                ],
            ),
            "gl_tests_validating": targets.mixin(
                args = [
                    "--test-launcher-filter-file=../../testing/buildbot/filters/android.emulator_12_12l_13.gl_tests.filter",
                ],
            ),
            "media_unittests": targets.mixin(
                args = [
                    "--test-launcher-filter-file=../../testing/buildbot/filters/android.emulator.media_unittests.filter",
                ],
            ),
            "perfetto_unittests": targets.mixin(
                args = [
                    # TODO(crbug.com/40201873): Fix the failed test
                    "--gtest_filter=-ScopedDirTest.CloseOutOfScope",
                ],
            ),
            "services_unittests": targets.mixin(
                swarming = targets.swarming(
                    shards = 2,
                ),
            ),
            "webview_instrumentation_test_apk_multiple_process_mode": targets.mixin(
                swarming = targets.swarming(
                    shards = 12,
                ),
            ),
        },
    ),
    targets_settings = targets.settings(
        os_type = targets.os_type.ANDROID,
    ),
    console_view_entry = consoles.console_view_entry(
        category = "emulator|x64|rel",
        short_name = "13",
    ),
    contact_team_email = "clank-engprod@google.com",
    # Android x64 builds take longer than x86 builds to compile
    # So they need longer timeouts
    # Matching the execution time out of the android-12-x64-rel
    execution_timeout = 4 * time.hour,
)

ci.builder(
    name = "android-14-x64-fyi-rel",
    description_html = "Run chromium tests on Android 14 emulators for experimental.",
    # Set to trigger manually as there is no experiment at the moment.
    triggered_by = [],
    builder_spec = builder_config.builder_spec(
        gclient_config = builder_config.gclient_config(
            config = "chromium",
            apply_configs = [
                "android",
            ],
        ),
        chromium_config = builder_config.chromium_config(
            config = "main_builder",
            apply_configs = [
                "mb",
            ],
            build_config = builder_config.build_config.RELEASE,
            target_arch = builder_config.target_arch.INTEL,
            target_bits = 64,
            target_platform = builder_config.target_platform.ANDROID,
        ),
        android_config = builder_config.android_config(
            config = "base_config",
        ),
        build_gs_bucket = "chromium-android-archive",
    ),
    gn_args = gn_args.config(
        configs = [
            "android_builder",
            "release_builder",
            "remoteexec",
            "minimal_symbols",
            "x64",
            "strip_debug_info",
            "android_fastbuild",
            "webview_trichrome",
            "webview_shell",
        ],
    ),
    targets = targets.bundle(
        targets = [
            "android_14_emulator_gtests",
        ],
        mixins = [
            "14-x64-emulator",
            "emulator-8-cores",
            "has_native_resultdb_integration",
            "linux-jammy",
            "x86-64",
        ],
        per_test_modifications = {
            "android_browsertests": targets.mixin(
                args = [
                    # https://crbug.com/361042311
                    "--gtest_filter=-All/SharedStorageChromeBrowserTest.CrossOriginWorklet_SelectURL_Success/*",
                ],
            ),
            "android_sync_integration_tests": targets.mixin(
                args = [
                    "--emulator-debug-tags=all,-qemud,-sensors",
                ],
                # https://crbug.com/345579530
                experiment_percentage = 100,
                swarming = targets.swarming(
                    shards = 2,
                ),
            ),
            "base_unittests": targets.mixin(
                args = [
                    "--test-launcher-filter-file=../../testing/buildbot/filters/android.emulator_14_15_16.base_unittests.filter",
                ],
            ),
            "chrome_public_test_apk": targets.mixin(
                args = [
                    "--test-launcher-filter-file=../../testing/buildbot/filters/android.emulator_14.chrome_public_test_apk.filter",
                    "--emulator-debug-tags=all,-qemud,-sensors",
                ],
                swarming = targets.swarming(
                    shards = 40,
                ),
            ),
            "chrome_public_unit_test_apk": targets.mixin(
                args = [
                    "--test-launcher-filter-file=../../testing/buildbot/filters/android.emulator_14.chrome_public_unit_test_apk.filter",
                ],
            ),
            "components_browsertests": targets.mixin(
                args = [
                    # TODO(crbug.com/40746860): Fix the test failure
                    "--gtest_filter=-V8ContextTrackerTest.AboutBlank",
                ],
            ),
            "components_unittests": targets.mixin(
                args = [
                    # crbug.com/361638641
                    "--gtest_filter=-BrowsingTopicsStateTest.EpochsForSite_FourEpochs_SwitchTimeArrived",
                ],
            ),
            "content_browsertests": targets.mixin(
                args = [
                    "--test-launcher-filter-file=../../testing/buildbot/filters/android.emulator_14.content_browsertests.filter",
                ],
                swarming = targets.swarming(
                    shards = 40,
                ),
            ),
            "content_shell_test_apk": targets.mixin(
                args = [
                    "--test-launcher-filter-file=../../testing/buildbot/filters/android.emulator_14_15_16.content_shell_test_apk.filter",
                ],
            ),
            "content_unittests": targets.mixin(
                args = [
                    "--test-launcher-filter-file=../../testing/buildbot/filters/android.emulator_14.content_unittests.filter",
                ],
            ),
            "crashpad_tests": targets.mixin(
                args = [
                    "--test-launcher-filter-file=../../testing/buildbot/filters/android.emulator.crashpad_tests.filter",
                ],
            ),
            "device_unittests": targets.mixin(
                args = [
                    "--test-launcher-filter-file=../../testing/buildbot/filters/android.emulator.device_unittests.filter",
                ],
            ),
            "gl_tests_validating": targets.mixin(
                args = [
                    "--test-launcher-filter-file=../../testing/buildbot/filters/android.emulator_12_12l_13.gl_tests.filter",
                ],
            ),
            "gwp_asan_unittests": targets.mixin(
                args = [
                    "--test-launcher-filter-file=../../testing/buildbot/filters/android.emulator_15_16.gwp_asan_unittests.filter",
                ],
            ),
            "media_unittests": targets.mixin(
                args = [
                    "--test-launcher-filter-file=../../testing/buildbot/filters/android.emulator.media_unittests.filter",
                ],
            ),
            "perfetto_unittests": targets.mixin(
                args = [
                    # TODO(crbug.com/40201873): Fix the failed test
                    "--gtest_filter=-ScopedDirTest.CloseOutOfScope",
                ],
            ),
            "unit_tests": targets.mixin(
                args = [
                    "--test-launcher-filter-file=../../testing/buildbot/filters/android.emulator_14_15_16.unit_tests.filter",
                ],
            ),
            "webkit_unit_tests": targets.mixin(
                args = [
                    # https://crbug.com/352586409
                    "--gtest_filter=-All/HTMLPreloadScannerLCPPLazyLoadImageTest.TokenStreamMatcherWithLoadingLazy/*",
                ],
            ),
            "webview_instrumentation_test_apk_multiple_process_mode": targets.mixin(
                args = [
                    "--test-launcher-filter-file=../../testing/buildbot/filters/android.14.webview_instrumentation_test_apk.filter",
                ],
                swarming = targets.swarming(
                    shards = 12,
                ),
            ),
        },
    ),
    targets_settings = targets.settings(
        os_type = targets.os_type.ANDROID,
    ),
    console_view_entry = consoles.console_view_entry(
        category = "emulator|x64|rel",
        short_name = "14",
    ),
    contact_team_email = "clank-engprod@google.com",
    execution_timeout = 4 * time.hour,
)

ci.builder(
    name = "android-14-arm64-fyi-rel",
    description_html = "Run chromium tests on Android 14 devices",
    builder_spec = builder_config.builder_spec(
        gclient_config = builder_config.gclient_config(
            config = "chromium",
            apply_configs = [
                "android",
            ],
        ),
        chromium_config = builder_config.chromium_config(
            config = "main_builder",
            apply_configs = ["mb"],
            build_config = builder_config.build_config.RELEASE,
            target_arch = builder_config.target_arch.ARM,
            target_bits = 64,
            target_platform = builder_config.target_platform.ANDROID,
        ),
        android_config = builder_config.android_config(
            config = "base_config",
        ),
        build_gs_bucket = "chromium-android-archive",
    ),
    gn_args = gn_args.config(
        configs = [
            "android_builder",
            "android_with_static_analysis",
            "release_builder",
            "remoteexec",
            "minimal_symbols",
            "arm64",
            "strip_debug_info",
            "webview_trichrome",
        ],
    ),
    targets = targets.bundle(
        targets = [
            "chromium_android_scripts",
        ],
        mixins = [
            "has_native_resultdb_integration",
            "isolate_profile_data",
            "panther_on_14",
        ],
    ),
    targets_settings = targets.settings(
        os_type = targets.os_type.ANDROID,
    ),
    console_view_entry = consoles.console_view_entry(
        category = "builder_tester|arm64|rel",
        short_name = "14",
    ),
    contact_team_email = "clank-engprod@google.com",
    execution_timeout = 4 * time.hour,
)

ci.builder(
    name = "android-15-x64-fyi-rel",
    description_html = "Run chromium tests on Android 15 emulators.",
    builder_spec = builder_config.builder_spec(
        gclient_config = builder_config.gclient_config(
            config = "chromium",
            apply_configs = [
                "android",
            ],
        ),
        chromium_config = builder_config.chromium_config(
            config = "main_builder",
            apply_configs = ["mb"],
            build_config = builder_config.build_config.RELEASE,
            target_arch = builder_config.target_arch.INTEL,
            target_bits = 64,
            target_platform = builder_config.target_platform.ANDROID,
        ),
        android_config = builder_config.android_config(
            config = "base_config",
        ),
        build_gs_bucket = "chromium-android-archive",
    ),
    gn_args = gn_args.config(
        configs = [
            "android_builder",
            "release_builder",
            "remoteexec",
            "minimal_symbols",
            "x64",
            "strip_debug_info",
            "android_fastbuild",
            "webview_trichrome",
            "webview_shell",
        ],
    ),
    targets = targets.bundle(
        targets = [
            "android_15_emulator_fyi_gtests",
        ],
        mixins = [
            targets.mixin(
                args = [
                    "--emulator-debug-tags=all,-qemud,-sensors",
                ],
            ),
            "15-x64-emulator",
            "emulator-8-cores",
            "has_native_resultdb_integration",
            "linux-jammy",
            "x86-64",
        ],
    ),
    targets_settings = targets.settings(
        os_type = targets.os_type.ANDROID,
    ),
    console_view_entry = consoles.console_view_entry(
        category = "emulator|x64|rel",
        short_name = "15",
    ),
    # Android x64 builds take longer than x86 builds to compile
    # So they need longer timeouts
    contact_team_email = "clank-engprod@google.com",
    execution_timeout = 4 * time.hour,
)

ci.builder(
    name = "android-15-tablet-x64-fyi-rel",
    # TODO(crbug.com/376748979 ): Enable on branches once tests are stable
    # branch_selector = branches.selector.ANDROID_BRANCHES,
    description_html = "Run chromium tests on Android 15 tablet emulators.",
    builder_spec = builder_config.builder_spec(
        gclient_config = builder_config.gclient_config(
            config = "chromium",
            apply_configs = [
                "android",
            ],
        ),
        chromium_config = builder_config.chromium_config(
            config = "main_builder",
            apply_configs = [
                "mb",
            ],
            build_config = builder_config.build_config.RELEASE,
            target_arch = builder_config.target_arch.INTEL,
            target_bits = 64,
            target_platform = builder_config.target_platform.ANDROID,
        ),
        android_config = builder_config.android_config(
            config = "base_config",
        ),
        build_gs_bucket = "chromium-android-archive",
    ),
    gn_args = gn_args.config(
        configs = [
            "android_builder",
            "release_builder",
            "remoteexec",
            "minimal_symbols",
            "x64",
            "strip_debug_info",
            "android_fastbuild",
        ],
    ),
    targets = targets.bundle(
        targets = [
            "android_browsertests_fyi",
        ],
        mixins = [
            "15-tablet-x64-emulator",
            "emulator-8-cores",
            "has_native_resultdb_integration",
            "linux-jammy",
            "x86-64",
        ],
        per_test_modifications = {
            "android_browsertests": targets.mixin(
                args = [
                    # https://crbug.com/375086487
                    "--gtest_filter=-InstallableManagerBrowserTest.CheckManifestWithIconThatIsTooSmall",
                ],
            ),
        },
    ),
    targets_settings = targets.settings(
        os_type = targets.os_type.ANDROID,
    ),
    console_view_entry = consoles.console_view_entry(
        category = "builder_tester|x64",
        short_name = "15T",
    ),
    contact_team_email = "clank-engprod@google.com",
    execution_timeout = 4 * time.hour,
)

ci.builder(
    name = "android-16-x64-fyi-rel",
    description_html = "Run chromium tests on Android 16 emulators.",
    builder_spec = builder_config.builder_spec(
        gclient_config = builder_config.gclient_config(
            config = "chromium",
            apply_configs = [
                "android",
            ],
        ),
        chromium_config = builder_config.chromium_config(
            config = "main_builder",
            apply_configs = ["mb"],
            build_config = builder_config.build_config.RELEASE,
            target_arch = builder_config.target_arch.INTEL,
            target_bits = 64,
            target_platform = builder_config.target_platform.ANDROID,
        ),
        android_config = builder_config.android_config(
            config = "base_config",
        ),
        build_gs_bucket = "chromium-android-archive",
    ),
    gn_args = gn_args.config(
        configs = [
            "android_builder",
            "release_builder",
            "remoteexec",
            "minimal_symbols",
            "x64",
            "strip_debug_info",
            "android_fastbuild",
            "webview_trichrome",
            "webview_shell",
        ],
    ),
    targets = targets.bundle(
        targets = [
            "android_16_emulator_fyi_gtests",
        ],
        mixins = [
            targets.mixin(
                args = [
                    "--emulator-debug-tags=all",
                ],
            ),
            "16-x64-emulator",
            "emulator-8-cores",
            "has_native_resultdb_integration",
            "linux-jammy",
            "x86-64",
        ],
        per_test_modifications = {
        },
    ),
    targets_settings = targets.settings(
        os_type = targets.os_type.ANDROID,
    ),
    console_view_entry = consoles.console_view_entry(
        category = "emulator|x64|rel",
        short_name = "16",
    ),
    # Android x64 builds take longer than x86 builds to compile
    # So they need longer timeouts
    contact_team_email = "clank-engprod@google.com",
    execution_timeout = 4 * time.hour,
)

ci.builder(
    name = "android-annotator-rel",
    builder_spec = builder_config.builder_spec(
        gclient_config = builder_config.gclient_config(
            config = "chromium",
            apply_configs = ["android"],
        ),
        chromium_config = builder_config.chromium_config(
            config = "main_builder",
            apply_configs = ["mb"],
            build_config = builder_config.build_config.RELEASE,
            target_arch = builder_config.target_arch.ARM,
            target_bits = 64,
            target_platform = builder_config.target_platform.ANDROID,
        ),
        android_config = builder_config.android_config(config = "base_config"),
        build_gs_bucket = "chromium-android-archive",
    ),
    gn_args = gn_args.config(
        configs = [
            "android_builder",
            "android_with_static_analysis",
            "release_builder",
            "remoteexec",
            "minimal_symbols",
            "arm64",
            "strip_debug_info",
            "webview_google",
        ],
    ),
    targets = targets.bundle(
        targets = [
            "test_traffic_annotation_auditor_script",
        ],
    ),
    targets_settings = targets.settings(
        os_type = targets.os_type.ANDROID,
    ),
    console_view_entry = consoles.console_view_entry(
        category = "network|traffic|annotations",
        short_name = "and",
    ),
    notifies = ["annotator-rel"],
)

# TODO(crbug.com/40216047): Move to non-FYI once the tester works fine.
ci.thin_tester(
    name = "android-webview-12-x64-dbg-tests",
    parent = "Android x64 Builder (dbg)",
    builder_spec = builder_config.builder_spec(
        execution_mode = builder_config.execution_mode.TEST,
        gclient_config = builder_config.gclient_config(
            config = "chromium",
            apply_configs = [
                "android",
            ],
        ),
        chromium_config = builder_config.chromium_config(
            config = "main_builder",
            apply_configs = ["mb"],
            build_config = builder_config.build_config.DEBUG,
            target_arch = builder_config.target_arch.INTEL,
            target_bits = 64,
            target_platform = builder_config.target_platform.ANDROID,
        ),
        android_config = builder_config.android_config(
            config = "base_config",
        ),
        build_gs_bucket = "chromium-android-archive",
    ),
    targets = targets.bundle(
        targets = [
            "webview_fyi_bot_all_gtests",
        ],
        mixins = [
            "12-x64-emulator",
            "emulator-8-cores",
            "has_native_resultdb_integration",
            "linux-jammy",
            "x86-64",
        ],
    ),
    targets_settings = targets.settings(
        os_type = targets.os_type.ANDROID,
    ),
    console_view_entry = consoles.console_view_entry(
        category = "tester|webview",
        short_name = "12",
    ),
)

ci.thin_tester(
    name = "android-webview-13-x64-dbg-tests",
    parent = "Android x64 Builder (dbg)",
    builder_spec = builder_config.builder_spec(
        execution_mode = builder_config.execution_mode.TEST,
        gclient_config = builder_config.gclient_config(
            config = "chromium",
            apply_configs = [
                "android",
            ],
        ),
        chromium_config = builder_config.chromium_config(
            config = "main_builder",
            apply_configs = ["mb"],
            build_config = builder_config.build_config.DEBUG,
            target_arch = builder_config.target_arch.INTEL,
            target_bits = 64,
            target_platform = builder_config.target_platform.ANDROID,
        ),
        android_config = builder_config.android_config(
            config = "base_config",
        ),
        build_gs_bucket = "chromium-android-archive",
    ),
    targets = targets.bundle(
        targets = [
            "webview_trichrome_64_cts_gtests",
            "webview_trichrome_64_32_cts_tests_suite",
        ],
        mixins = [
            "13-x64-emulator",
            "emulator-8-cores",
            "has_native_resultdb_integration",
            "linux-jammy",
            "x86-64",
        ],
    ),
    targets_settings = targets.settings(
        os_type = targets.os_type.ANDROID,
    ),
    console_view_entry = consoles.console_view_entry(
        category = "tester|webview",
        short_name = "13",
    ),
    notifies = [],
)

# TODO(crbug.com/40216047): Move to non-FYI once the tester works fine.
ci.thin_tester(
    name = "android-12-x64-dbg-tests",
    parent = "Android x64 Builder (dbg)",
    builder_spec = builder_config.builder_spec(
        execution_mode = builder_config.execution_mode.TEST,
        gclient_config = builder_config.gclient_config(
            config = "chromium",
            apply_configs = [
                "android",
            ],
        ),
        chromium_config = builder_config.chromium_config(
            config = "main_builder",
            apply_configs = ["mb"],
            build_config = builder_config.build_config.DEBUG,
            target_arch = builder_config.target_arch.INTEL,
            target_bits = 64,
            target_platform = builder_config.target_platform.ANDROID,
        ),
        android_config = builder_config.android_config(
            config = "base_config",
        ),
        build_gs_bucket = "chromium-android-archive",
    ),
    targets = targets.bundle(
        targets = [
            "android_12_dbg_emulator_gtests",
        ],
        mixins = [
            "12-x64-emulator",
            "emulator-8-cores",
            "has_native_resultdb_integration",
            "linux-jammy",
            "x86-64",
        ],
    ),
    targets_settings = targets.settings(
        os_type = targets.os_type.ANDROID,
    ),
    console_view_entry = consoles.console_view_entry(
        category = "tester|phone",
        short_name = "12",
    ),
)

ci.builder(
    name = "android-cronet-asan-x86-rel",
    builder_spec = builder_config.builder_spec(
        execution_mode = builder_config.execution_mode.COMPILE_AND_TEST,
        gclient_config = builder_config.gclient_config(
            config = "chromium",
            apply_configs = ["android"],
        ),
        chromium_config = builder_config.chromium_config(
            config = "main_builder",
            apply_configs = [
                "cronet_builder",
                "mb",
            ],
            build_config = builder_config.build_config.RELEASE,
            target_arch = builder_config.target_arch.INTEL,
            target_bits = 32,
            target_platform = builder_config.target_platform.ANDROID,
        ),
        android_config = builder_config.android_config(
            config = "base_config",
        ),
    ),
    gn_args = gn_args.config(
        configs = [
            "android_builder",
            "android_with_static_analysis",
            "cronet_android",
            "release_builder",
            "remoteexec",
            "minimal_symbols",
            "x86",
            "clang",
            "asan",
            "strip_debug_info",
        ],
    ),
    targets = targets.bundle(
        targets = [
            "cronet_gtests",
        ],
        additional_compile_targets = [
            "cronet_package",
            "cronet_perf_test_apk",
        ],
        mixins = [
            "marshmallow-x86-emulator",
            "emulator-4-cores",
            "has_native_resultdb_integration",
            "linux-jammy",
            "x86-64",
        ],
    ),
    targets_settings = targets.settings(
        os_type = targets.os_type.ANDROID,
    ),
    console_view_entry = consoles.console_view_entry(
        category = "cronet|asan",
    ),
    contact_team_email = "cronet-team@google.com",
)
