# Copyright 2023 The Chromium Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Definitions of builders in `build` bucket."""
# TODO: crbug.com/1502025 - Reduce duplicated configs from the shadow builders.
# Note that CI builders can't use `mirrors`.

load("//lib/builder_config.star", "builder_config")
load("//lib/builders.star", "cpu", "os", "siso")
load("//lib/ci.star", "ci")
load("//lib/consoles.star", "consoles")
load("//lib/gn_args.star", "gn_args")
load("//lib/html.star", "linkify_builder")
load("//lib/xcode.star", "xcode")
load("//project.star", "settings")

luci.bucket(
    name = "build",
    acls = [
        acl.entry(
            roles = acl.BUILDBUCKET_READER,
            groups = "all",
        ),
        acl.entry(
            roles = acl.BUILDBUCKET_TRIGGERER,
            groups = [
                "project-chromium-ci-schedulers",
            ],
        ),
        acl.entry(
            roles = acl.BUILDBUCKET_OWNER,
            groups = [
                "project-chromium-admins",
                "mdb/chrome-ops-browser-build-team",
            ],
        ),
        acl.entry(
            roles = acl.SCHEDULER_TRIGGERER,
            groups = "project-chromium-scheduler-triggerers",
        ),
        acl.entry(
            roles = acl.SCHEDULER_OWNER,
            groups = [
                "mdb/chrome-troopers",
            ],
        ),
    ],
)

# Define the shadow bucket of `build`.
# See also http://go/luci-how-to-led#configure-a-shadow-bucket
luci.bucket(
    name = "build.shadow",
    shadows = "build",
    constraints = luci.bucket_constraints(
        pools = [ci.DEFAULT_POOL],
        service_accounts = [
            "chromium-build-perf-ci-builder@chops-service-accounts.iam.gserviceaccount.com",
        ],
    ),
    bindings = [
        # for led permissions.
        luci.binding(
            roles = "role/buildbucket.creator",
            groups = [
                "chromium-led-users",
                "mdb/chrome-build-access-sphinx",
                "mdb/chrome-troopers",
                "mdb/foundry-x-team",
            ],
        ),
    ],
    dynamic = True,
)

luci.gitiles_poller(
    name = "chrome-build-gitiles-trigger",
    bucket = "build",
    repo = "https://chromium.googlesource.com/chromium/src",
    refs = [settings.ref],
)

ci.defaults.set(
    bucket = "build",
    triggered_by = ["chrome-build-gitiles-trigger"],
    builder_group = "chromium.build",
    pool = ci.DEFAULT_POOL,
    builderless = False,
    # rely on the builder dimension for the bot selection.
    cores = None,
    build_numbers = True,
    contact_team_email = "chrome-build-team@google.com",
    execution_timeout = 10 * time.hour,
    priority = ci.DEFAULT_FYI_PRIORITY,
    resultdb_enable = False,
    service_account = "chromium-build-perf-ci-builder@chops-service-accounts.iam.gserviceaccount.com",
    siso_configs = [],
    siso_experiments = ["no-fallback"],
)

consoles.console_view(
    name = "chromium.build",
    repo = "https://chromium.googlesource.com/chromium/src",
)

def cq_build_perf_builder(description_html, **kwargs):
    # Use CQ RBE instance and high remote_jobs/cores to simulate CQ builds.
    if not "siso_configs" in kwargs:
        kwargs["siso_configs"] = ["builder", "remote-link"]
    props = {}
    if not kwargs.get("siso_enabled", True):
        props["$build/reclient"] = {
            "instance": siso.project.DEFAULT_UNTRUSTED,
            "jobs": 500,
            "metrics_project": "chromium-reclient-metrics",
            "scandeps_server": True,
        }
    return ci.builder(
        description_html = description_html + "<br>Build stats is show in http://shortn/_gaAdI3x6o6.",
        siso_remote_jobs = siso.remote_jobs.HIGH_JOBS_FOR_CQ,
        siso_project = siso.project.DEFAULT_UNTRUSTED,
        use_clang_coverage = True,
        properties = props,
        **kwargs
    )

def ci_build_perf_builder(description_html, **kwargs):
    # Use CI RBE instance to simulate CI builds.
    if not "siso_configs" in kwargs:
        kwargs["siso_configs"] = ["builder", "remote-link"]
    return ci.builder(
        description_html = description_html + "<br>Build stats is show in http://shortn/_gaAdI3x6o6.",
        siso_remote_jobs = siso.remote_jobs.DEFAULT,
        siso_project = siso.project.DEFAULT_TRUSTED,
        **kwargs
    )

cq_build_perf_builder(
    name = "android-build-perf-ninja",
    description_html = "This builder measures Android CQ build performance with Ninja.<br/>" +
                       "The build configs and the bot specs should be in sync with " + linkify_builder("try", "android-arm64-rel-compilator", "chromium"),
    executable = "recipe:chrome_build/build_perf",
    builder_spec = builder_config.builder_spec(
        gclient_config = builder_config.gclient_config(
            config = "chromium",
            apply_configs = [
                "ninja_staging",
                "android",
            ],
        ),
        chromium_config = builder_config.chromium_config(
            config = "main_builder",
            apply_configs = [
                "mb",
            ],
            build_config = builder_config.build_config.RELEASE,
            target_arch = builder_config.target_arch.ARM,
            target_bits = 64,
            target_platform = builder_config.target_platform.ANDROID,
        ),
        android_config = builder_config.android_config(
            config = "base_config",
        ),
    ),
    gn_args = gn_args.config(configs = ["try/android-arm64-rel", "reclient", "no_siso"]),
    os = os.LINUX_DEFAULT,
    console_view_entry = consoles.console_view_entry(
        category = "android",
        short_name = "ninja",
    ),
    siso_enabled = False,
)

cq_build_perf_builder(
    name = "android-build-perf-siso",
    description_html = "This builder measures Android CQ build performance with Siso.<br/>" +
                       "The build configs and the bot specs should be in sync with " + linkify_builder("try", "android-arm64-rel-compilator", "chromium"),
    executable = "recipe:chrome_build/build_perf_siso",
    builder_spec = builder_config.builder_spec(
        gclient_config = builder_config.gclient_config(
            config = "chromium",
            apply_configs = [
                "android",
                "siso_latest",
            ],
        ),
        chromium_config = builder_config.chromium_config(
            config = "main_builder",
            apply_configs = [
                "mb",
            ],
            build_config = builder_config.build_config.RELEASE,
            target_arch = builder_config.target_arch.ARM,
            target_bits = 64,
            target_platform = builder_config.target_platform.ANDROID,
        ),
        android_config = builder_config.android_config(
            config = "base_config",
        ),
    ),
    gn_args = {
        "builtin": gn_args.config(configs = ["try/android-arm64-rel", "no_reclient"]),
    },
    os = os.LINUX_DEFAULT,
    console_view_entry = consoles.console_view_entry(
        category = "android",
        short_name = "siso",
    ),
)

cq_build_perf_builder(
    name = "linux-build-perf-ninja",
    description_html = "This builder measures Linux CQ build performance with Ninja.<br/>" +
                       "The build configs and the bot specs should be in sync with " + linkify_builder("try", "linux-rel-compilator", "chromium"),
    executable = "recipe:chrome_build/build_perf",
    builder_spec = builder_config.builder_spec(
        gclient_config = builder_config.gclient_config(
            config = "chromium",
            apply_configs = [
                "android",
                "ninja_staging",
            ],
        ),
        chromium_config = builder_config.chromium_config(
            config = "chromium",
            apply_configs = [
                "mb",
            ],
            target_platform = builder_config.target_platform.LINUX,
        ),
    ),
    gn_args = gn_args.config(configs = ["try/linux-rel", "reclient", "no_siso"]),
    os = os.LINUX_DEFAULT,
    console_view_entry = consoles.console_view_entry(
        category = "linux",
        short_name = "ninja",
    ),
    siso_enabled = False,
)

cq_build_perf_builder(
    name = "linux-build-perf-siso",
    description_html = "This builder measures Linux CQ build performance with Siso.<br/>" +
                       "The build configs and the bot specs should be in sync with " + linkify_builder("try", "linux-rel-compilator", "chromium"),
    executable = "recipe:chrome_build/build_perf_siso",
    builder_spec = builder_config.builder_spec(
        gclient_config = builder_config.gclient_config(
            config = "chromium",
            apply_configs = [
                "siso_latest",
            ],
        ),
        chromium_config = builder_config.chromium_config(
            config = "chromium",
            apply_configs = [
                "mb",
            ],
            target_platform = builder_config.target_platform.LINUX,
        ),
    ),
    gn_args = {
        "builtin": gn_args.config(configs = ["try/linux-rel", "no_reclient"]),
    },
    os = os.LINUX_DEFAULT,
    console_view_entry = consoles.console_view_entry(
        category = "linux",
        short_name = "siso",
    ),
)

cq_build_perf_builder(
    name = "win-build-perf-ninja",
    description_html = "This builder measures Windows CQ build performance with Ninja.<br/>" +
                       "The build configs and the bot specs should be in sync with " + linkify_builder("try", "win-rel-compilator", "chromium"),
    executable = "recipe:chrome_build/build_perf",
    builder_spec = builder_config.builder_spec(
        gclient_config = builder_config.gclient_config(
            config = "chromium",
            apply_configs = [
                "ninja_staging",
            ],
        ),
        chromium_config = builder_config.chromium_config(
            config = "chromium",
            apply_configs = [
                "mb",
            ],
            target_platform = builder_config.target_platform.WIN,
        ),
    ),
    gn_args = gn_args.config(configs = ["try/win-rel", "reclient", "no_siso"]),
    os = os.WINDOWS_DEFAULT,
    console_view_entry = consoles.console_view_entry(
        category = "windows",
        short_name = "ninja",
    ),
    siso_enabled = False,
)

cq_build_perf_builder(
    name = "win-build-perf-siso",
    description_html = "This builder measures Windows CQ build performance with Siso.<br/>" +
                       "The build configs and the bot specs should be in sync with " + linkify_builder("try", "win-rel-compilator", "chromium"),
    executable = "recipe:chrome_build/build_perf_siso",
    builder_spec = builder_config.builder_spec(
        gclient_config = builder_config.gclient_config(
            config = "chromium",
            apply_configs = [
                "siso_latest",
            ],
        ),
        chromium_config = builder_config.chromium_config(
            config = "chromium",
            apply_configs = [
                "mb",
            ],
            target_platform = builder_config.target_platform.WIN,
        ),
    ),
    gn_args = {
        "builtin": gn_args.config(configs = ["try/win-rel", "no_reclient"]),
    },
    os = os.WINDOWS_DEFAULT,
    console_view_entry = consoles.console_view_entry(
        category = "windows",
        short_name = "siso",
    ),
)

ci_build_perf_builder(
    name = "win-build-perf-ci-siso",
    description_html = "This builder measures Windows CI build performance with Siso.<br/>" +
                       "The build configs and the bot specs should be in sync with " + linkify_builder("ci", " Win x64 Builder", "chromium"),
    executable = "recipe:chrome_build/build_perf_siso",
    builder_spec = builder_config.builder_spec(
        gclient_config = builder_config.gclient_config(
            config = "chromium",
            apply_configs = [
                "siso_latest",
            ],
        ),
        chromium_config = builder_config.chromium_config(
            config = "chromium",
            apply_configs = [
                "mb",
            ],
            target_platform = builder_config.target_platform.WIN,
        ),
    ),
    gn_args = {
        "builtin": gn_args.config(configs = ["ci/Win x64 Builder", "no_reclient"]),
    },
    os = os.WINDOWS_DEFAULT,
    console_view_entry = consoles.console_view_entry(
        category = "windows",
        short_name = "sisoci",
    ),
    siso_configs = ["builder"],
    # TODO(333491525): enable no-fallback once OOM fallback mitigated.
    siso_experiments = [],
    siso_limits = "fastlocal=0",
)

cq_build_perf_builder(
    name = "linux-chromeos-build-perf-ninja",
    description_html = "This builder measures CrOS CQ build performance with Ninja.<br/>" +
                       "The build configs and the bot specs should be in sync with " + linkify_builder("try", "linux-chromeos-rel-compilator", "chromium"),
    executable = "recipe:chrome_build/build_perf",
    builder_spec = builder_config.builder_spec(
        gclient_config = builder_config.gclient_config(
            config = "chromium",
            apply_configs = [
                "chromeos",
                "ninja_staging",
            ],
        ),
        chromium_config = builder_config.chromium_config(
            config = "chromium",
            apply_configs = [
                "mb",
            ],
            target_platform = builder_config.target_platform.CHROMEOS,
        ),
    ),
    gn_args = gn_args.config(configs = ["try/linux-chromeos-rel", "reclient", "no_siso"]),
    os = os.LINUX_DEFAULT,
    console_view_entry = consoles.console_view_entry(
        category = "cros",
        short_name = "ninja",
    ),
    siso_enabled = False,
)

cq_build_perf_builder(
    name = "linux-chromeos-build-perf-siso",
    description_html = "This builder measures CrOS CQ build performance with Siso.<br/>" +
                       "The build configs and the bot specs should be in sync with " + linkify_builder("try", "linux-chromeos-rel-compilator", "chromium"),
    executable = "recipe:chrome_build/build_perf_siso",
    builder_spec = builder_config.builder_spec(
        gclient_config = builder_config.gclient_config(
            config = "chromium",
            apply_configs = [
                "chromeos",
                "siso_latest",
            ],
        ),
        chromium_config = builder_config.chromium_config(
            config = "chromium",
            apply_configs = [
                "mb",
            ],
            target_platform = builder_config.target_platform.CHROMEOS,
        ),
    ),
    gn_args = {
        "builtin": gn_args.config(configs = ["try/linux-chromeos-rel", "no_reclient"]),
    },
    os = os.LINUX_DEFAULT,
    console_view_entry = consoles.console_view_entry(
        category = "cros",
        short_name = "siso",
    ),
)

cq_build_perf_builder(
    name = "mac-build-perf-ninja",
    description_html = "This builder measures Mac CQ build performance with Ninja.<br/>" +
                       "The build configs and the bot specs should be in sync with " + linkify_builder("try", "mac-rel-compilator", "chromium"),
    executable = "recipe:chrome_build/build_perf",
    builder_spec = builder_config.builder_spec(
        gclient_config = builder_config.gclient_config(
            config = "chromium",
            apply_configs = [
                "ninja_staging",
            ],
        ),
        chromium_config = builder_config.chromium_config(
            config = "chromium",
            apply_configs = [
                "mb",
            ],
            build_config = builder_config.build_config.RELEASE,
            target_bits = 64,
            target_platform = builder_config.target_platform.MAC,
        ),
    ),
    gn_args = gn_args.config(configs = ["try/mac-rel", "reclient", "no_siso"]),
    os = os.MAC_DEFAULT,
    cpu = cpu.ARM64,
    console_view_entry = consoles.console_view_entry(
        category = "mac",
        short_name = "ninja",
    ),
    siso_configs = ["builder"],
    siso_enabled = False,
)

cq_build_perf_builder(
    name = "mac-build-perf-siso",
    description_html = "This builder measures Mac CQ build performance with Siso.<br/>" +
                       "The build configs and the bot specs should be in sync with " + linkify_builder("try", "mac-rel-compilator", "chromium"),
    executable = "recipe:chrome_build/build_perf_siso",
    builder_spec = builder_config.builder_spec(
        gclient_config = builder_config.gclient_config(
            config = "chromium",
            apply_configs = [
                "siso_latest",
            ],
        ),
        chromium_config = builder_config.chromium_config(
            config = "chromium",
            apply_configs = [
                "mb",
            ],
            build_config = builder_config.build_config.RELEASE,
            target_bits = 64,
            target_platform = builder_config.target_platform.MAC,
        ),
    ),
    gn_args = {
        "builtin": gn_args.config(configs = ["try/mac-rel", "no_reclient"]),
    },
    os = os.MAC_DEFAULT,
    cpu = cpu.ARM64,
    console_view_entry = consoles.console_view_entry(
        category = "mac",
        short_name = "siso",
    ),
    siso_configs = ["builder"],
)

cq_build_perf_builder(
    name = "ios-build-perf-ninja",
    description_html = "This builder measures iOS CQ build performance with Ninja.<br/>" +
                       "The build configs and the bot specs should be in sync with " + linkify_builder("try", "ios-simulator-compilator", "chromium"),
    executable = "recipe:chrome_build/build_perf",
    builder_spec = builder_config.builder_spec(
        gclient_config = builder_config.gclient_config(
            config = "ios",
            apply_configs = [
                "ninja_staging",
            ],
        ),
        chromium_config = builder_config.chromium_config(
            config = "chromium",
            apply_configs = [
                "mb",
                "mac_toolchain",
            ],
            build_config = builder_config.build_config.DEBUG,
            target_bits = 64,
            target_platform = builder_config.target_platform.IOS,
        ),
    ),
    gn_args = gn_args.config(configs = ["try/ios-simulator", "reclient", "no_siso"]),
    os = os.MAC_DEFAULT,
    cpu = cpu.ARM64,
    console_view_entry = consoles.console_view_entry(
        category = "ios",
        short_name = "ninja",
    ),
    siso_configs = ["builder"],
    siso_enabled = False,
    xcode = xcode.xcode_default,
)

cq_build_perf_builder(
    name = "ios-build-perf-siso",
    description_html = "This builder measures iOS CQ build performance with Siso.<br/>" +
                       "The build configs and the bot specs should be in sync with " + linkify_builder("try", "ios-simulator-compilator", "chromium"),
    executable = "recipe:chrome_build/build_perf_siso",
    builder_spec = builder_config.builder_spec(
        gclient_config = builder_config.gclient_config(
            config = "ios",
            apply_configs = [
                "siso_latest",
            ],
        ),
        chromium_config = builder_config.chromium_config(
            config = "chromium",
            apply_configs = [
                "mb",
                "mac_toolchain",
            ],
            build_config = builder_config.build_config.DEBUG,
            target_bits = 64,
            target_platform = builder_config.target_platform.IOS,
        ),
    ),
    gn_args = {
        "builtin": gn_args.config(configs = ["try/ios-simulator", "no_reclient"]),
    },
    os = os.MAC_DEFAULT,
    cpu = cpu.ARM64,
    console_view_entry = consoles.console_view_entry(
        category = "ios",
        short_name = "siso",
    ),
    siso_configs = ["builder"],
    xcode = xcode.xcode_default,
)

def developer_build_perf_builder(description_html, reclient_jobs = None, **kwargs):
    # Use CQ siso.project and high siso_remote_jobs/cores to simulate CQ builds.
    if not "siso_configs" in kwargs:
        kwargs["siso_configs"] = ["remote-link"]
    props = {
        "$build/reclient": {
            "instance": siso.project.DEFAULT_UNTRUSTED,
            "jobs": reclient_jobs,
            "metrics_project": "chromium-reclient-metrics",
            "scandeps_server": True,
        },
    }
    return ci.builder(
        description_html = description_html + "<br>Build stats is show in http://shortn/_gaAdI3x6o6.",
        executable = "recipe:chrome_build/build_perf_developer",
        # developer build usually interactive and not-batch build.
        siso_disable_batch_mode = True,
        siso_project = siso.project.DEFAULT_UNTRUSTED,
        shadow_siso_project = None,
        properties = props,
        **kwargs
    )

developer_build_perf_builder(
    name = "android-build-perf-developer",
    description_html = """\
This builder measures build performance for Android developer builds, by simulating developer build scenarios on a high spec bot.\
""",
    builder_spec = builder_config.builder_spec(
        gclient_config = builder_config.gclient_config(
            config = "chromium",
            apply_configs = [
                "android",
                "siso_latest",
                "ninja_staging",
            ],
        ),
        chromium_config = builder_config.chromium_config(
            config = "main_builder",
            apply_configs = [
                "mb",
            ],
            build_config = builder_config.build_config.DEBUG,
            target_arch = builder_config.target_arch.ARM,
            target_bits = 64,
            target_platform = builder_config.target_platform.ANDROID,
        ),
        android_config = builder_config.android_config(
            config = "base_config",
        ),
    ),
    gn_args = {
        "ninja": gn_args.config(configs = ["android_developer", "android_fastbuild", "remoteexec", "no_siso", "reclient"]),
        "siso_native": gn_args.config(configs = ["android_developer", "android_fastbuild", "remoteexec", "no_reclient"]),
    },
    os = os.LINUX_DEFAULT,
    console_view_entry = consoles.console_view_entry(
        category = "android",
        short_name = "dev",
    ),
    reclient_jobs = 5120,
    siso_remote_jobs = 5120,
)

developer_build_perf_builder(
    name = "linux-build-perf-developer",
    description_html = """\
This builder measures build performance for Linux developer builds, by simulating developer build scenarios on a high spec bot.\
""",
    builder_spec = builder_config.builder_spec(
        gclient_config = builder_config.gclient_config(
            config = "chromium",
            apply_configs = [
                "siso_latest",
            ],
        ),
        chromium_config = builder_config.chromium_config(
            config = "chromium",
            apply_configs = [
                "mb",
            ],
            target_platform = builder_config.target_platform.LINUX,
        ),
    ),
    gn_args = {
        "ninja": gn_args.config(configs = ["developer", "remoteexec", "no_siso", "reclient", "linux", "x64"]),
        "siso_native": gn_args.config(configs = ["developer", "remoteexec", "no_reclient", "linux", "x64"]),
    },
    os = os.LINUX_DEFAULT,
    console_view_entry = consoles.console_view_entry(
        category = "linux",
        short_name = "dev",
    ),
    reclient_jobs = 5120,
    siso_remote_jobs = 5120,
)

developer_build_perf_builder(
    name = "win-build-perf-developer",
    description_html = """\
This builder measures build performance for Windows developer builds, by simulating developer build scenarios on a high spec bot.\
""",
    builder_spec = builder_config.builder_spec(
        gclient_config = builder_config.gclient_config(
            config = "chromium",
            apply_configs = [
                "siso_latest",
            ],
        ),
        chromium_config = builder_config.chromium_config(
            config = "chromium",
            apply_configs = [
                "mb",
            ],
            target_platform = builder_config.target_platform.WIN,
        ),
    ),
    gn_args = {
        "ninja": gn_args.config(configs = ["developer", "remoteexec", "no_siso", "reclient", "win", "x64"]),
        "siso_native": gn_args.config(configs = ["developer", "remoteexec", "no_reclient", "win", "x64"]),
    },
    os = os.WINDOWS_DEFAULT,
    console_view_entry = consoles.console_view_entry(
        category = "windows",
        short_name = "dev",
    ),
    reclient_jobs = 1000,
    siso_remote_jobs = 5120,  # Siso doesn't set remote limit for Window builds.
)

developer_build_perf_builder(
    name = "mac-build-perf-developer",
    description_html = """\
This builder measures build performance for Mac developer builds, by simulating developer build scenarios on a bot.\
""",
    builder_spec = builder_config.builder_spec(
        gclient_config = builder_config.gclient_config(
            config = "chromium",
            apply_configs = [
                "siso_latest",
            ],
        ),
        chromium_config = builder_config.chromium_config(
            config = "chromium",
            apply_configs = [
                "mb",
            ],
            target_platform = builder_config.target_platform.MAC,
        ),
    ),
    gn_args = {
        "ninja": gn_args.config(configs = ["developer", "remoteexec", "no_siso", "reclient", "mac", "arm64"]),
        "siso_native": gn_args.config(configs = ["developer", "remoteexec", "no_reclient", "mac", "arm64"]),
    },
    os = os.MAC_DEFAULT,
    cpu = cpu.ARM64,
    console_view_entry = consoles.console_view_entry(
        category = "mac",
        short_name = "dev",
    ),
    reclient_jobs = 640,
    siso_configs = [],
    siso_remote_jobs = 5120,  # Siso doesn't set remote limit for Mac builds.
)

developer_build_perf_builder(
    name = "ios-build-perf-developer",
    description_html = """\
This builder measures build performance for iOS developer builds, by simulating developer build scenarios on a bot.\
""",
    builder_spec = builder_config.builder_spec(
        gclient_config = builder_config.gclient_config(
            config = "ios",
            apply_configs = [
                "siso_latest",
            ],
        ),
        chromium_config = builder_config.chromium_config(
            config = "chromium",
            apply_configs = [
                "mb",
                "mac_toolchain",
            ],
            build_config = builder_config.build_config.DEBUG,
            target_bits = 64,
            target_platform = builder_config.target_platform.IOS,
        ),
    ),
    gn_args = {
        "ninja": gn_args.config(configs = ["ios_developer", "remoteexec", "no_siso", "reclient", "arm64"]),
        "siso_native": gn_args.config(configs = ["ios_developer", "remoteexec", "no_reclient", "arm64"]),
    },
    os = os.MAC_DEFAULT,
    cpu = cpu.ARM64,
    console_view_entry = consoles.console_view_entry(
        category = "ios",
        short_name = "dev",
    ),
    reclient_jobs = 640,
    siso_configs = [],
    siso_remote_jobs = 5120,  # Siso doesn't set remote limit for iOS builds.
    xcode = xcode.xcode_default,
)

# Experimental builder set up to track local CPU time for Chromium build. b/333389736
ci.builder(
    name = "linux-build-perf-no-rbe",
    description_html = "Monitoring CPU time to build `chrome` target locally without remote executions",
    executable = "recipe:chrome_build/build_perf_without_rbe",
    builder_spec = builder_config.builder_spec(
        gclient_config = builder_config.gclient_config(
            config = "chromium",
        ),
        chromium_config = builder_config.chromium_config(
            config = "chromium",
            apply_configs = ["mb"],
            build_config = builder_config.build_config.RELEASE,
            target_bits = 64,
            target_platform = builder_config.target_platform.LINUX,
        ),
    ),
    gn_args = gn_args.config(
        args = {
            # For analyze_includes.py
            "show_includes": True,
        },
        configs = [
            "no_remoteexec",
            "linux",
            "x64",
        ],
    ),
    os = os.LINUX_DEFAULT,
    console_view_entry = consoles.console_view_entry(
        category = "linux",
        short_name = "norbe",
    ),
    contact_team_email = "chrome-build-team@google.com",
    notifies = ["Chromium Build Time Watcher"],
    siso_fail_if_reapi_used = True,
    siso_project = siso.project.DEFAULT_UNTRUSTED,
)
