# Copyright 2018 The Chromium Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# pylint: disable=too-many-lines

import os
import six.moves.urllib.parse  # pylint: disable=import-error

from core import benchmark_finders
from core import benchmark_utils

from telemetry.story import story_filter


_SHARD_MAP_DIR = os.path.join(os.path.dirname(__file__), 'shard_maps')

_ALL_BENCHMARKS_BY_NAMES = dict(
    (b.Name(), b) for b in benchmark_finders.GetAllBenchmarks())

OFFICIAL_BENCHMARKS = frozenset(
    b for b in benchmark_finders.GetOfficialBenchmarks() if b.IsScheduled())
CONTRIB_BENCHMARKS = frozenset(benchmark_finders.GetContribBenchmarks())
ALL_SCHEDULEABLE_BENCHMARKS = OFFICIAL_BENCHMARKS | CONTRIB_BENCHMARKS
GTEST_STORY_NAME = '_gtest_'


def _IsPlatformSupported(benchmark, platform):
  supported = benchmark.GetSupportedPlatformNames(benchmark.SUPPORTED_PLATFORMS)
  return 'all' in supported or platform in supported


class PerfPlatform(object):
  def __init__(self,
               name,
               description,
               benchmark_configs,
               num_shards,
               platform_os,
               is_fyi=False,
               is_calibration=False,
               run_reference_build=False,
               pinpoint_only=False,
               executables=None,
               crossbench=None):
    benchmark_configs = benchmark_configs.Frozenset()
    self._name = name
    self._description = description
    self._platform_os = platform_os
    # For sorting ignore case and "segments" in the bot name.
    self._sort_key = name.lower().replace('-', ' ')
    self._is_fyi = is_fyi
    self._is_calibration = is_calibration
    self.run_reference_build = run_reference_build
    self.pinpoint_only = pinpoint_only
    self.executables = executables or frozenset()
    self.crossbench = crossbench or frozenset()
    assert num_shards
    self._num_shards = num_shards
    # pylint: disable=redefined-outer-name
    self._benchmark_configs = frozenset([
        b for b in benchmark_configs if
          _IsPlatformSupported(b.benchmark, self._platform_os)])
    # pylint: enable=redefined-outer-name
    benchmark_names = [config.name for config in self._benchmark_configs]
    assert len(set(benchmark_names)) == len(benchmark_names), (
        'Make sure that a benchmark does not appear twice.')

    base_file_name = name.replace(' ', '_').lower()
    self._timing_file_path = os.path.join(
        _SHARD_MAP_DIR, 'timing_data', base_file_name + '_timing.json')
    self.shards_map_file_name = base_file_name + '_map.json'
    self._shards_map_file_path = os.path.join(
        _SHARD_MAP_DIR, self.shards_map_file_name)

  def __lt__(self, other):
    if not isinstance(other, type(self)):
      return NotImplemented
    # pylint: disable=protected-access
    return self._sort_key < other._sort_key

  @property
  def num_shards(self):
    return self._num_shards

  @property
  def shards_map_file_path(self):
    return self._shards_map_file_path

  @property
  def timing_file_path(self):
    return self._timing_file_path

  @property
  def name(self):
    return self._name

  @property
  def description(self):
    return self._description

  @property
  def platform(self):
    return self._platform_os

  @property
  def benchmarks_to_run(self):
    # TODO(crbug.com/40628256): Deprecate this in favor of benchmark_configs
    # as part of change to make sharding scripts accommodate abridged
    # benchmarks.
    return frozenset({b.benchmark for b in self._benchmark_configs})

  @property
  def benchmark_configs(self):
    return self._benchmark_configs

  @property
  def is_fyi(self):
    return self._is_fyi

  @property
  def is_calibration(self):
    return self._is_calibration

  @property
  def is_official(self):
    return not self._is_fyi and not self.is_calibration

  @property
  def builder_url(self):
    if self.pinpoint_only:
      return None
    return ('https://ci.chromium.org/p/chrome/builders/ci/%s' %
            six.moves.urllib.parse.quote(self._name))


class BenchmarkConfig(object):

  def __init__(self, benchmark, abridged, pageset_repeat_override):
    """A configuration for a benchmark that helps decide how to shard it.

    Args:
      benchmark: the benchmark.Benchmark object.
      abridged: True if the benchmark should be abridged so fewer stories
        are run, and False if the whole benchmark should be run.
      pageset_repeat_override: number of times to repeat the entire story set.
        can be None, which defaults to the benchmark default pageset_repeat.
    """
    self.benchmark = benchmark
    self.abridged = abridged
    self._stories = None
    self._exhaustive_stories = None
    self.is_telemetry = True
    self.pageset_repeat_override = pageset_repeat_override

  @property
  def name(self):
    return self.benchmark.Name()

  @property
  def repeat(self):
    if self.pageset_repeat_override is not None:
      return self.pageset_repeat_override
    return self.benchmark.options.get('pageset_repeat', 1)

  @property
  def stories(self):
    if self._stories is not None:
      return self._stories
    story_set = benchmark_utils.GetBenchmarkStorySet(self.benchmark())
    abridged_story_set_tag = (story_set.GetAbridgedStorySetTagFilter()
                              if self.abridged else None)
    story_filter_obj = story_filter.StoryFilter(
        abridged_story_set_tag=abridged_story_set_tag)
    stories = story_filter_obj.FilterStories(story_set)
    self._stories = [story.name for story in stories]
    return self._stories

  @property
  def exhaustive_stories(self):
    if self._exhaustive_stories is not None:
      return self._exhaustive_stories
    story_set = benchmark_utils.GetBenchmarkStorySet(self.benchmark(),
                                                     exhaustive=True)
    abridged_story_set_tag = (story_set.GetAbridgedStorySetTagFilter()
                              if self.abridged else None)
    story_filter_obj = story_filter.StoryFilter(
        abridged_story_set_tag=abridged_story_set_tag)
    stories = story_filter_obj.FilterStories(story_set)
    self._exhaustive_stories = [story.name for story in stories]
    return self._exhaustive_stories


class ExecutableConfig(object):
  def __init__(self, name, path=None, flags=None, estimated_runtime=60):
    self.name = name
    self.path = path or name
    self.flags = flags or []
    self.estimated_runtime = estimated_runtime
    self.abridged = False
    self.stories = [GTEST_STORY_NAME]
    self.is_telemetry = False
    self.repeat = 1


class CrossbenchConfig:

  def __init__(self,
               name,
               crossbench_name,
               estimated_runtime=60,
               stories=None,
               arguments=None):
    self.name = name
    self.crossbench_name = crossbench_name
    self.estimated_runtime = estimated_runtime
    self.stories = stories or ['default']
    self.arguments = arguments or []
    self.repeat = 1


class PerfSuite(object):
  def __init__(self, configs):
    self._configs = dict()
    self.Add(configs)

  def Frozenset(self):
    return frozenset(self._configs.values())

  def Add(self, configs):
    if isinstance(configs, PerfSuite):
      configs = configs.Frozenset()
    for config in configs:
      if isinstance(config, str):
        config = _GetBenchmarkConfig(config)
      if config.name in self._configs:
        raise ValueError('Cannot have duplicate benchmarks/executables.')
      self._configs[config.name] = config
    return self

  def Remove(self, configs):
    for config in configs:
      name = config
      if isinstance(config, PerfSuite):
        name = config.name
      del self._configs[name]
    return self

  def Abridge(self, config_names):
    for name in config_names:
      del self._configs[name]
      self._configs[name] = _GetBenchmarkConfig(
          name, abridged=True)
    return self

  def Repeat(self, config_names, pageset_repeat):
    for name in config_names:
      self._configs[name] = _GetBenchmarkConfig(
          name,
          abridged=self._configs[name].abridged,
          pageset_repeat=pageset_repeat)
    return self


def _GetBenchmarkConfig(benchmark_name, abridged=False, pageset_repeat=None):
  benchmark = _ALL_BENCHMARKS_BY_NAMES[benchmark_name]
  return BenchmarkConfig(benchmark, abridged, pageset_repeat)

OFFICIAL_BENCHMARK_CONFIGS = PerfSuite(
    [_GetBenchmarkConfig(b.Name()) for b in OFFICIAL_BENCHMARKS])
OFFICIAL_BENCHMARK_CONFIGS = OFFICIAL_BENCHMARK_CONFIGS.Remove([
    'blink_perf.svg',
    'blink_perf.paint',
    'jetstream2-minorms',
    'octane-minorms',
    'speedometer2-minorms',
    'speedometer2-predictable',
    'speedometer3-minorms',
    'speedometer3-predictable',
])
# TODO(crbug.com/40628256): Remove OFFICIAL_BENCHMARK_NAMES once sharding
# scripts are no longer using it.
OFFICIAL_BENCHMARK_NAMES = frozenset(
    b.name for b in OFFICIAL_BENCHMARK_CONFIGS.Frozenset())

def _sync_performance_tests(estimated_runtime=110,
                            path=None,
                            additional_flags=None):
  if not additional_flags:
    additional_flags = []
  flags = ['--test-launcher-jobs=1', '--test-launcher-retry-limit=0']
  flags.extend(additional_flags)
  return ExecutableConfig('sync_performance_tests',
                          path=path,
                          flags=flags,
                          estimated_runtime=estimated_runtime)


def _base_perftests(estimated_runtime=270, path=None, additional_flags=None):
  if not additional_flags:
    additional_flags = []
  flags = ['--test-launcher-jobs=1', '--test-launcher-retry-limit=0']
  flags.extend(additional_flags)
  return ExecutableConfig('base_perftests',
                          path=path,
                          flags=flags,
                          estimated_runtime=estimated_runtime)


def _components_perftests(estimated_runtime=110):
  return ExecutableConfig('components_perftests',
                          flags=[
                              '--xvfb',
                          ],
                          estimated_runtime=estimated_runtime)


def _dawn_perf_tests(estimated_runtime=270):
  return ExecutableConfig(
      'dawn_perf_tests',
      flags=['--test-launcher-jobs=1', '--test-launcher-retry-limit=0'],
      estimated_runtime=estimated_runtime)


def _tint_benchmark(estimated_runtime=180):
  return ExecutableConfig('tint_benchmark',
                          flags=['--use-chrome-perf-format'],
                          estimated_runtime=estimated_runtime)


def _load_library_perf_tests(estimated_runtime=3):
  return ExecutableConfig('load_library_perf_tests',
                          estimated_runtime=estimated_runtime)


def _performance_browser_tests(estimated_runtime=67):
  return ExecutableConfig(
      'performance_browser_tests',
      path='browser_tests',
      flags=[
          '--full-performance-run',
          '--test-launcher-jobs=1',
          '--test-launcher-retry-limit=0',
          # Allow the full performance runs to take up to 60 seconds (rather
          # than the default of 30 for normal CQ browser test runs).
          '--ui-test-action-timeout=60000',
          '--ui-test-action-max-timeout=60000',
          '--test-launcher-timeout=60000',
          '--gtest_filter=*/TabCapturePerformanceTest.*:'
          '*/CastV2PerformanceTest.*',
      ],
      estimated_runtime=estimated_runtime)


def _tracing_perftests(estimated_runtime=50):
  return ExecutableConfig('tracing_perftests',
                          estimated_runtime=estimated_runtime)


def _views_perftests(estimated_runtime=7):
  return ExecutableConfig('views_perftests',
                          flags=['--xvfb'],
                          estimated_runtime=estimated_runtime)


# Speedometer:
def _speedometer2_0_crossbench(estimated_runtime=60, arguments=()):
  return CrossbenchConfig('speedometer2.0.crossbench',
                          'speedometer_2.0',
                          estimated_runtime=estimated_runtime,
                          arguments=arguments)


def _speedometer2_1_crossbench(estimated_runtime=60, arguments=()):
  return CrossbenchConfig('speedometer2.1.crossbench',
                          'speedometer_2.1',
                          estimated_runtime=estimated_runtime,
                          arguments=arguments)


def _speedometer2_crossbench(estimated_runtime=60, arguments=()):
  """Alias for the latest Speedometer 2.X version."""
  return CrossbenchConfig('speedometer2.crossbench',
                          'speedometer_2',
                          estimated_runtime=estimated_runtime,
                          arguments=arguments)


def _speedometer3_0_crossbench(estimated_runtime=60, arguments=()):
  return CrossbenchConfig('speedometer3.0.crossbench',
                          'speedometer_3.0',
                          estimated_runtime=estimated_runtime,
                          arguments=arguments)


def _speedometer3_1_crossbench(estimated_runtime=60, arguments=()):
  return CrossbenchConfig('speedometer3.1.crossbench',
                          'speedometer_3.1',
                          estimated_runtime=estimated_runtime,
                          arguments=arguments)


def _speedometer3_crossbench(estimated_runtime=60, arguments=()):
  """Alias for the latest Speedometer 3.X version."""
  return CrossbenchConfig('speedometer3.crossbench',
                          'speedometer_3',
                          estimated_runtime=estimated_runtime,
                          arguments=arguments)


def _speedometer_main_crossbench(estimated_runtime=60, arguments=()):
  arguments += ("--detailed-metrics")
  return CrossbenchConfig('speedometer_main.crossbench',
                          'speedometer_main',
                          estimated_runtime=estimated_runtime,
                          arguments=arguments)


# MotionMark:
def _motionmark1_2_crossbench(estimated_runtime=360, arguments=()):
  return CrossbenchConfig('motionmark1.2.crossbench',
                          'motionmark_1.2',
                          estimated_runtime=estimated_runtime,
                          arguments=arguments)


def _motionmark1_3_0_crossbench(estimated_runtime=360, arguments=()):
  return CrossbenchConfig('motionmark1.3.0.crossbench',
                          'motionmark_1.3.0',
                          estimated_runtime=estimated_runtime,
                          arguments=arguments)


def _motionmark1_3_1_crossbench(estimated_runtime=360, arguments=()):
  return CrossbenchConfig('motionmark1.3.1.crossbench',
                          'motionmark_1.3.1',
                          estimated_runtime=estimated_runtime,
                          arguments=arguments)


def _motionmark1_3_crossbench(estimated_runtime=360, arguments=()):
  """Alias for the latest MotionMark 1.3.X version."""
  return CrossbenchConfig('motionmark1.3.crossbench',
                          'motionmark_1.3',
                          estimated_runtime=estimated_runtime,
                          arguments=arguments)


def _motionmark_main_crossbench(estimated_runtime=360, arguments=()):
  return CrossbenchConfig('motionmark_main.crossbench',
                          'motionmark_main',
                          estimated_runtime=estimated_runtime,
                          arguments=arguments)


# JetStream:
def _jetstream2_0_crossbench(estimated_runtime=180, arguments=()):
  return CrossbenchConfig('jetstream2.0.crossbench',
                          'jetstream_2.0',
                          estimated_runtime=estimated_runtime,
                          arguments=arguments)


def _jetstream2_1_crossbench(estimated_runtime=180, arguments=()):
  return CrossbenchConfig('jetstream2.1.crossbench',
                          'jetstream_2.1',
                          estimated_runtime=estimated_runtime,
                          arguments=arguments)


def _jetstream2_2_crossbench(estimated_runtime=180, arguments=()):
  return CrossbenchConfig('jetstream2.2.crossbench',
                          'jetstream_2.2',
                          estimated_runtime=estimated_runtime,
                          arguments=arguments)


def _jetstream2_crossbench(estimated_runtime=180, arguments=()):
  """Alias of the latest JetStream 2.X version."""
  return CrossbenchConfig('jetstream2.crossbench',
                          'jetstream_2',
                          estimated_runtime=estimated_runtime,
                          arguments=arguments)


def _jetstream_main_crossbench(estimated_runtime=180, arguments=()):
  return CrossbenchConfig('jetstream_main.crossbench',
                          'jetstream_main',
                          estimated_runtime=estimated_runtime,
                          arguments=arguments)


# LoadLine:
def _loadline_phone_crossbench(estimated_runtime=7000, arguments=()):
  return CrossbenchConfig('loadline_phone.crossbench',
                          'loadline-phone-fast',
                          estimated_runtime=estimated_runtime,
                          arguments=arguments)


def _loadline_tablet_crossbench(estimated_runtime=3600, arguments=()):
  return CrossbenchConfig('loadline_tablet.crossbench',
                          'loadline-tablet-fast',
                          estimated_runtime=estimated_runtime,
                          arguments=arguments)


# Webview:
def _crossbench_loading(estimated_runtime=60, arguments=None):
  return CrossbenchConfig('loading.crossbench',
                          'loading',
                          estimated_runtime=estimated_runtime,
                          arguments=arguments)


def _crossbench_embedder(estimated_runtime=20, arguments=None):
  return CrossbenchConfig('embedder.crossbench',
                          'embedder',
                          estimated_runtime=estimated_runtime,
                          arguments=arguments)


_CROSSBENCH_JETSTREAM_SPEEDOMETER = frozenset([
    _jetstream2_crossbench(),
    _speedometer3_crossbench(),
])

_CROSSBENCH_MOTIONMARK_SPEEDOMETER = frozenset([
    _motionmark1_3_crossbench(),
    _speedometer3_crossbench(),
])

_CROSSBENCH_BENCHMARKS_ALL = frozenset([
    _speedometer2_crossbench(),
    _speedometer3_crossbench(),
    _motionmark1_3_crossbench(),
    _jetstream2_crossbench(),
])

# TODO(crbug.com/338630584): Remove it when other benchmarks can be run on
# Android.
_CROSSBENCH_ANDROID = frozenset([
    _speedometer3_crossbench(arguments=['--fileserver']),
    _loadline_phone_crossbench(arguments=[
        '--cool-down-threshold=moderate',
        '--no-splash',
    ]),
])

# TODO(crbug.com/409326154): Enable crossbench variant when supported.
# TODO(crbug.com/409571674): Remove --debug flag.
_CROSSBENCH_PIXEL9 = frozenset([
    # _jetstream2_crossbench(arguments=['--fileserver', '--debug']),
    _motionmark1_3_crossbench(arguments=['--fileserver', '--debug']),
    _speedometer3_crossbench(arguments=['--fileserver', '--debug']),
    _loadline_phone_crossbench(arguments=[
        '--cool-down-threshold=moderate',
        '--no-splash',
        '--debug',
    ]),
])

_CROSSBENCH_ANDROID_AL = frozenset([
    _speedometer3_crossbench(arguments=['--fileserver', '--debug']),
])

_CROSSBENCH_TANGOR = frozenset([
    _loadline_tablet_crossbench(arguments=[
        '--cool-down-threshold=moderate',
        '--no-splash',
    ]),
])

_CROSSBENCH_WEBVIEW = frozenset([
    _crossbench_loading(
        estimated_runtime=750,
        arguments=[
            '--wpr=crossbench_android_loading_000.wprgo',
            '--probe=chrome_histograms:{"baseline":false,"metrics":'
            '{"Android.WebView.Startup.CreationTime.StartChromiumLocked":["mean"],'
            '"Android.WebView.Startup.CreationTime.Stage1.FactoryInit":["mean"],'
            '"PageLoad.PaintTiming.NavigationToFirstContentfulPaint":["mean"]}}',
            '--repetitions=50',
            '--stories=cnn',
        ]
    ),
    _crossbench_embedder(
        estimated_runtime=900,
        arguments=[
            '--wpr=crossbench_android_embedder_000.wprgo',
            '--skip-wpr-script-injection',
            '--embedder=com.google.android.googlequicksearchbox',
            '--splashscreen=skip',
            '--cuj-config=../../third_party/crossbench/config/team/woa/embedder_cuj_config.hjson',
            '--probe-config=../../clank/android_webview/tools/crossbench_config/'
            'agsa_probe_config.hjson',
            '--repetitions=50',
        ]
    ),
])

_CHROME_HEALTH_BENCHMARK_CONFIGS_DESKTOP = PerfSuite(
    [_GetBenchmarkConfig('system_health.common_desktop')])

FUCHSIA_EXEC_ARGS = {
    'astro': None,
    'sherlock': None,
    'atlas': None,
    'nelson': None,
    'nuc': None
}
_IMAGE_PATHS = {
    'astro': ('astro-release', 'smart_display_eng_arrested'),
    'sherlock': ('sherlock-release', 'smart_display_max_eng_arrested'),
    'nelson': ('nelson-release', 'smart_display_m3_eng_paused'),
}

# TODO(zijiehe): Fuchsia should check the os version, i.e. --os-check=check, but
# perf test run multiple suites in sequential and the os checks are performed
# multiple times. Currently there isn't a simple way to check only once at the
# beginning of the test.
# See the revision:
# https://crsrc.org/c/tools/perf/core/bot_platforms.py
#   ;drc=93a804bc8c5871e1fb70a762e461d787749cb2d7;l=470
_COMMON_FUCHSIA_ARGS = ['-d', '--os-check=ignore']
for board, path_parts in _IMAGE_PATHS.items():
  FUCHSIA_EXEC_ARGS[board] = _COMMON_FUCHSIA_ARGS

_LINUX_BENCHMARK_CONFIGS = PerfSuite(OFFICIAL_BENCHMARK_CONFIGS).Remove([
    'v8.runtime_stats.top_25',
]).Add([
    'blink_perf.svg',
    'blink_perf.paint',
])
_LINUX_BENCHMARK_CONFIGS_WITH_MINORMS_PREDICTABLE = PerfSuite(
    _LINUX_BENCHMARK_CONFIGS).Add([
        'jetstream2-minorms',
        'octane-minorms',
        'speedometer2-minorms',
        'speedometer2-predictable',
        'speedometer3-minorms',
        'speedometer3-predictable',
    ])
_LINUX_EXECUTABLE_CONFIGS = frozenset([
    # TODO(crbug.com/40562709): Add views_perftests.
    _base_perftests(200),
    _load_library_perf_tests(),
    _tint_benchmark(),
    _tracing_perftests(5),
])
_LINUX_R350_BENCHMARK_CONFIGS = PerfSuite(
    _LINUX_BENCHMARK_CONFIGS_WITH_MINORMS_PREDICTABLE).Remove([
        'rendering.desktop',
        'rendering.desktop.notracing',
        'system_health.common_desktop',
    ])
_MAC_INTEL_BENCHMARK_CONFIGS = PerfSuite(OFFICIAL_BENCHMARK_CONFIGS).Remove([
    'v8.runtime_stats.top_25',
    'rendering.desktop',
])
_MAC_INTEL_EXECUTABLE_CONFIGS = frozenset([
    _base_perftests(300),
    _dawn_perf_tests(330),
    _tint_benchmark(),
    _views_perftests(),
    _load_library_perf_tests(),
])
_MAC_M1_MINI_2020_BENCHMARK_CONFIGS = PerfSuite(
    OFFICIAL_BENCHMARK_CONFIGS).Remove([
        'v8.runtime_stats.top_25',
    ]).Add([
        'jetstream2-minorms',
        'jetstream2-no-field-trials',
        'speedometer2-minorms',
        'speedometer3-minorms',
        'speedometer3-no-field-trials',
    ]).Repeat([
        'speedometer2',
        'rendering.desktop.notracing',
    ], 2).Repeat([
        'speedometer3',
        'speedometer3-no-field-trials',
    ], 6).Repeat([
        'jetstream2',
        'jetstream2-no-field-trials',
    ], 11)
_MAC_M1_MINI_2020_PGO_BENCHMARK_CONFIGS = PerfSuite([
    _GetBenchmarkConfig('jetstream2', pageset_repeat=11),
    _GetBenchmarkConfig('speedometer2'),
    _GetBenchmarkConfig('speedometer3', pageset_repeat=22),
    _GetBenchmarkConfig('rendering.desktop.notracing'),
])
_MAC_M1_MINI_2020_NO_BRP_BENCHMARK_CONFIGS = PerfSuite([
    _GetBenchmarkConfig('speedometer2', pageset_repeat=2),
    _GetBenchmarkConfig('speedometer3', pageset_repeat=2),
    _GetBenchmarkConfig('rendering.desktop.notracing', pageset_repeat=2),
])
_MAC_M1_PRO_BENCHMARK_CONFIGS = PerfSuite([
    _GetBenchmarkConfig('jetstream2'),
    _GetBenchmarkConfig('speedometer2'),
    _GetBenchmarkConfig('speedometer3'),
    _GetBenchmarkConfig('rendering.desktop.notracing'),
])
_MAC_M1_MINI_2020_EXECUTABLE_CONFIGS = frozenset([
    _base_perftests(300),
    _dawn_perf_tests(330),
    _tint_benchmark(),
    _views_perftests(),
])
_MAC_M2_PRO_BENCHMARK_CONFIGS = PerfSuite(OFFICIAL_BENCHMARK_CONFIGS).Remove([
    'v8.runtime_stats.top_25',
]).Add([
    'jetstream2-minorms',
    'speedometer2-minorms',
    'speedometer3-minorms',
])
_MAC_M3_PRO_BENCHMARK_CONFIGS = PerfSuite([])

_WIN_10_BENCHMARK_CONFIGS = PerfSuite(OFFICIAL_BENCHMARK_CONFIGS).Remove([
    'v8.runtime_stats.top_25',
])
_WIN_10_EXECUTABLE_CONFIGS = frozenset([
    _base_perftests(200),
    _components_perftests(125),
    _dawn_perf_tests(600),
    _views_perftests(),
])
_WIN_10_LOW_END_BENCHMARK_CONFIGS = PerfSuite(OFFICIAL_BENCHMARK_CONFIGS)
_WIN_10_LOW_END_HP_CANDIDATE_BENCHMARK_CONFIGS = PerfSuite([
    _GetBenchmarkConfig('v8.browsing_desktop'),
    _GetBenchmarkConfig('rendering.desktop', abridged=True),
])
_WIN_10_AMD_LAPTOP_BENCHMARK_CONFIGS = PerfSuite([
    _GetBenchmarkConfig('jetstream2'),
    _GetBenchmarkConfig('octane'),
    _GetBenchmarkConfig('speedometer2'),
    _GetBenchmarkConfig('speedometer3'),
])
_WIN_11_BENCHMARK_CONFIGS = PerfSuite(OFFICIAL_BENCHMARK_CONFIGS).Remove([
    'rendering.desktop',
    'rendering.desktop.notracing',
    'system_health.common_desktop',
    'v8.runtime_stats.top_25',
])
_WIN_11_EXECUTABLE_CONFIGS = frozenset([
    _base_perftests(200),
    _components_perftests(125),
    _dawn_perf_tests(600),
    _tint_benchmark(),
    _views_perftests(),
])
_WIN_ARM64_BENCHMARK_CONFIGS = PerfSuite([
    _GetBenchmarkConfig('blink_perf.dom'),
    _GetBenchmarkConfig('jetstream2'),
    _GetBenchmarkConfig('media.desktop'),
    _GetBenchmarkConfig('rendering.desktop', abridged=True),
    _GetBenchmarkConfig('rendering.desktop.notracing'),
    _GetBenchmarkConfig('speedometer2'),
    _GetBenchmarkConfig('speedometer3'),
    _GetBenchmarkConfig('system_health.common_desktop'),
    _GetBenchmarkConfig('v8.browsing_desktop'),
])
_WIN_ARM64_EXECUTABLE_CONFIGS = frozenset([
    _base_perftests(200),
    _components_perftests(125),
    _views_perftests(),
])
_WIN_11_LOW_END_BENCHMARK_CONFIGS = _WIN_ARM64_BENCHMARK_CONFIGS
_ANDROID_GO_BENCHMARK_CONFIGS = PerfSuite([
    _GetBenchmarkConfig('system_health.memory_mobile'),
    _GetBenchmarkConfig('system_health.common_mobile'),
    _GetBenchmarkConfig('startup.mobile'),
    _GetBenchmarkConfig('system_health.webview_startup'),
    _GetBenchmarkConfig('v8.browsing_mobile'),
    _GetBenchmarkConfig('speedometer'),
    _GetBenchmarkConfig('speedometer2'),
    _GetBenchmarkConfig('speedometer3'),
])
_ANDROID_DEFAULT_EXECUTABLE_CONFIGS = frozenset([
    _components_perftests(60),
])
_ANDROID_GO_WEBVIEW_BENCHMARK_CONFIGS = _ANDROID_GO_BENCHMARK_CONFIGS
_ANDROID_PIXEL4_BENCHMARK_CONFIGS = PerfSuite(OFFICIAL_BENCHMARK_CONFIGS)
_ANDROID_PIXEL4_WEBVIEW_BENCHMARK_CONFIGS = PerfSuite(
    OFFICIAL_BENCHMARK_CONFIGS).Remove([
        'jetstream2',
        'v8.browsing_mobile-future',
    ])
_ANDROID_PIXEL6_BENCHMARK_CONFIGS = PerfSuite(OFFICIAL_BENCHMARK_CONFIGS).Add(
    [_GetBenchmarkConfig('system_health.scroll_jank_mobile')]).Repeat([
        'speedometer3',
    ], 4)
_ANDROID_PIXEL6_PGO_BENCHMARK_CONFIGS = PerfSuite([
    _GetBenchmarkConfig('system_health.common_mobile'),
    _GetBenchmarkConfig('jetstream2'),
    _GetBenchmarkConfig('rendering.mobile'),
    _GetBenchmarkConfig('speedometer2'),
    _GetBenchmarkConfig('speedometer2-predictable'),
    _GetBenchmarkConfig('speedometer3', pageset_repeat=16),
    _GetBenchmarkConfig('speedometer3-predictable'),
])
_ANDROID_PIXEL6_PRO_BENCHMARK_CONFIGS = PerfSuite(
    OFFICIAL_BENCHMARK_CONFIGS).Add([
        _GetBenchmarkConfig('jetstream2-minorms'),
        _GetBenchmarkConfig('speedometer2-minorms'),
        _GetBenchmarkConfig('speedometer3-minorms'),
    ])
# TODO(crbug.com/409326154): Remove these for the crossbench variants when
# supported.
_ANDROID_PIXEL9_BENCHMARK_CONFIGS = PerfSuite([
    _GetBenchmarkConfig('jetstream2'),
])
_ANDROID_PIXEL_FOLD_BENCHMARK_CONFIGS = PerfSuite(
    OFFICIAL_BENCHMARK_CONFIGS).Add([
        _GetBenchmarkConfig('jetstream2-minorms'),
        _GetBenchmarkConfig('speedometer2-minorms'),
        _GetBenchmarkConfig('speedometer3-minorms'),
    ])
_ANDROID_PIXEL_TANGOR_BENCHMARK_CONFIGS = PerfSuite(
    OFFICIAL_BENCHMARK_CONFIGS).Add([
        _GetBenchmarkConfig('jetstream2-minorms'),
        _GetBenchmarkConfig('speedometer2-minorms'),
        _GetBenchmarkConfig('speedometer3-minorms')
    ])
# Android Desktop (AL)
_ANDROID_AL_BENCHMARK_CONFIGS = PerfSuite([
    _GetBenchmarkConfig('rendering.mobile'),
])

_CHROMEOS_KEVIN_FYI_BENCHMARK_CONFIGS = PerfSuite(
    [_GetBenchmarkConfig('rendering.desktop')])
_FUCHSIA_PERF_SMARTDISPLAY_BENCHMARK_CONFIGS = PerfSuite([
    _GetBenchmarkConfig('speedometer2'),
    _GetBenchmarkConfig('media.mobile'),
    _GetBenchmarkConfig('v8.browsing_mobile'),
])
_LINUX_PERF_FYI_BENCHMARK_CONFIGS = PerfSuite([
    _GetBenchmarkConfig('speedometer2'),
    _GetBenchmarkConfig('speedometer2-minorms'),
    _GetBenchmarkConfig('speedometer3'),
])
_LINUX_PERF_CALIBRATION_BENCHMARK_CONFIGS = PerfSuite([
    _GetBenchmarkConfig('speedometer2'),
    _GetBenchmarkConfig('speedometer3'),
    _GetBenchmarkConfig('blink_perf.shadow_dom'),
    _GetBenchmarkConfig('system_health.common_desktop'),
])


# Linux
LINUX_PGO = PerfPlatform('linux-perf-pgo',
                         'Ubuntu-18.04, 8 core, NVIDIA Quadro P400',
                         _LINUX_BENCHMARK_CONFIGS,
                         26,
                         'linux',
                         executables=_LINUX_EXECUTABLE_CONFIGS,
                         pinpoint_only=True)
LINUX_REL = PerfPlatform(
    'linux-perf-rel', 'Ubuntu-18.04, 8 core, NVIDIA Quadro P400',
    _CHROME_HEALTH_BENCHMARK_CONFIGS_DESKTOP, 2,
    'linux', executables=_LINUX_EXECUTABLE_CONFIGS)
LINUX_R350 = PerfPlatform('linux-r350-perf',
                          'Ubuntu-22.04, 16 core',
                          _LINUX_R350_BENCHMARK_CONFIGS,
                          30,
                          'linux',
                          executables=_LINUX_EXECUTABLE_CONFIGS,
                          crossbench=_CROSSBENCH_BENCHMARKS_ALL)

# Mac
MAC_INTEL = PerfPlatform('mac-intel-perf',
                         'Mac Mini 8,1, Core i7 3.2 GHz',
                         _MAC_INTEL_BENCHMARK_CONFIGS,
                         24,
                         'mac',
                         executables=_MAC_INTEL_EXECUTABLE_CONFIGS,
                         crossbench=_CROSSBENCH_BENCHMARKS_ALL)
MAC_M1_MINI_2020 = PerfPlatform(
    'mac-m1_mini_2020-perf',
    'Mac M1 Mini 2020',
    _MAC_M1_MINI_2020_BENCHMARK_CONFIGS,
    28,
    'mac',
    executables=_MAC_M1_MINI_2020_EXECUTABLE_CONFIGS,
    crossbench=_CROSSBENCH_BENCHMARKS_ALL)
MAC_M1_MINI_2020_PGO = PerfPlatform('mac-m1_mini_2020-perf-pgo',
                                    'Mac M1 Mini 2020',
                                    _MAC_M1_MINI_2020_PGO_BENCHMARK_CONFIGS,
                                    7,
                                    'mac',
                                    crossbench=_CROSSBENCH_BENCHMARKS_ALL)
MAC_M1_MINI_2020_NO_BRP = PerfPlatform(
    'mac-m1_mini_2020-no-brp-perf', 'Mac M1 Mini 2020 with BRP disabled',
    _MAC_M1_MINI_2020_NO_BRP_BENCHMARK_CONFIGS, 20, 'mac')
MAC_M1_PRO = PerfPlatform('mac-m1-pro-perf',
                          'Mac M1 PRO 2020',
                          _MAC_M1_PRO_BENCHMARK_CONFIGS,
                          4,
                          'mac',
                          crossbench=_CROSSBENCH_BENCHMARKS_ALL)
MAC_M2_PRO = PerfPlatform('mac-m2-pro-perf',
                          'Mac M2 PRO Baremetal ARM',
                          _MAC_M2_PRO_BENCHMARK_CONFIGS,
                          20,
                          'mac',
                          crossbench=_CROSSBENCH_BENCHMARKS_ALL)
MAC_M3_PRO = PerfPlatform('mac-m3-pro-perf',
                          'Mac M3 PRO ARM',
                          _MAC_M3_PRO_BENCHMARK_CONFIGS,
                          4,
                          'mac',
                          crossbench=_CROSSBENCH_BENCHMARKS_ALL)
# Win
WIN_10_LOW_END = PerfPlatform(
    'win-10_laptop_low_end-perf',
    'Low end windows 10 HP laptops. HD Graphics 5500, x86-64-i3-5005U, '
    'SSD, 4GB RAM.',
    _WIN_10_LOW_END_BENCHMARK_CONFIGS,
    # TODO(crbug.com/278947510): Increase the count when m.2 disks stop failing.
    45,
    'win',
    crossbench=_CROSSBENCH_BENCHMARKS_ALL)
WIN_10_LOW_END_PGO = PerfPlatform(
    'win-10_laptop_low_end-perf-pgo',
    'Low end windows 10 HP laptops. HD Graphics 5500, x86-64-i3-5005U, '
    'SSD, 4GB RAM.',
    _WIN_10_LOW_END_BENCHMARK_CONFIGS,
    # TODO(crbug.com/40218037): Increase the count back to 46 when issue fixed.
    40,
    'win',
    pinpoint_only=True)
WIN_10 = PerfPlatform(
    'win-10-perf',
    'Windows Intel HD 630 towers, Core i7-7700 3.6 GHz, 16GB RAM,'
    ' Intel Kaby Lake HD Graphics 630',
    _WIN_10_BENCHMARK_CONFIGS,
    18,
    'win',
    executables=_WIN_10_EXECUTABLE_CONFIGS,
    crossbench=_CROSSBENCH_BENCHMARKS_ALL)
WIN_10_PGO = PerfPlatform(
    'win-10-perf-pgo',
    'Windows Intel HD 630 towers, Core i7-7700 3.6 GHz, 16GB RAM,'
    ' Intel Kaby Lake HD Graphics 630',
    _WIN_10_BENCHMARK_CONFIGS,
    18,
    'win',
    executables=_WIN_10_EXECUTABLE_CONFIGS,
    pinpoint_only=True)
WIN_10_AMD_LAPTOP = PerfPlatform('win-10_amd_laptop-perf',
                                 'Windows 10 Laptop with AMD chipset.',
                                 _WIN_10_AMD_LAPTOP_BENCHMARK_CONFIGS,
                                 3,
                                 'win',
                                 crossbench=_CROSSBENCH_JETSTREAM_SPEEDOMETER)
WIN_10_AMD_LAPTOP_PGO = PerfPlatform('win-10_amd_laptop-perf-pgo',
                                     'Windows 10 Laptop with AMD chipset.',
                                     _WIN_10_AMD_LAPTOP_BENCHMARK_CONFIGS,
                                     3,
                                     'win',
                                     pinpoint_only=True)
WIN_11_LOW_END = PerfPlatform('win-11_laptop_low_end-perf',
                              'Low end windows 11 laptops.'
                              'SSD, 4GB RAM.',
                              _WIN_11_LOW_END_BENCHMARK_CONFIGS,
                              2,
                              'win',
                              crossbench=_CROSSBENCH_BENCHMARKS_ALL)
WIN_11 = PerfPlatform('win-11-perf',
                      'Windows Dell PowerEdge R350',
                      _WIN_11_BENCHMARK_CONFIGS,
                      20,
                      'win',
                      executables=_WIN_11_EXECUTABLE_CONFIGS,
                      crossbench=_CROSSBENCH_BENCHMARKS_ALL)
WIN_11_PGO = PerfPlatform('win-11-perf-pgo',
                          'Windows Dell PowerEdge R350',
                          _WIN_11_BENCHMARK_CONFIGS,
                          26,
                          'win',
                          executables=_WIN_11_EXECUTABLE_CONFIGS,
                          pinpoint_only=True)
WIN_ARM64_SNAPDRAGON_PLUS = PerfPlatform(
    'win-arm64-snapdragon-plus-perf',
    'Windows Dell Snapdragon Plus',
    _WIN_ARM64_BENCHMARK_CONFIGS,
    1,
    'win',
    executables=_WIN_ARM64_EXECUTABLE_CONFIGS,
    crossbench=_CROSSBENCH_BENCHMARKS_ALL,
    is_fyi=True)

# Android
ANDROID_BRYA = PerfPlatform(
    name='android-brya-kano-i5-8gb-perf',
    description='Brya SKU kano_12th_Gen_IntelR_CoreTM_i5_1235U_8GB',
    num_shards=7,
    benchmark_configs=_ANDROID_AL_BENCHMARK_CONFIGS,
    platform_os='android',
    executables=None,
    crossbench=_CROSSBENCH_ANDROID_AL)
ANDROID_CORSOLA = PerfPlatform(name='android-corsola-steelix-8gb-perf',
                               description='Corsola SKU steelix_MT8186_8GB',
                               num_shards=7,
                               benchmark_configs=_ANDROID_AL_BENCHMARK_CONFIGS,
                               platform_os='android',
                               executables=None,
                               crossbench=_CROSSBENCH_ANDROID_AL)
ANDROID_NISSA = PerfPlatform(name='android-nissa-uldren-8gb-perf',
                             description='Nissa SKU uldren_99C4LZ/Q1XT/6W_8GB',
                             num_shards=7,
                             benchmark_configs=_ANDROID_AL_BENCHMARK_CONFIGS,
                             platform_os='android',
                             executables=None,
                             crossbench=_CROSSBENCH_ANDROID_AL)
ANDROID_PIXEL4 = PerfPlatform('android-pixel4-perf',
                              'Android R',
                              _ANDROID_PIXEL4_BENCHMARK_CONFIGS,
                              44,
                              'android',
                              executables=_ANDROID_DEFAULT_EXECUTABLE_CONFIGS)
ANDROID_PIXEL4_PGO = PerfPlatform(
    'android-pixel4-perf-pgo',
    'Android R',
    _ANDROID_PIXEL4_BENCHMARK_CONFIGS,
    28,
    'android',
    executables=_ANDROID_DEFAULT_EXECUTABLE_CONFIGS,
    pinpoint_only=True)
ANDROID_PIXEL4_WEBVIEW = PerfPlatform(
    'android-pixel4_webview-perf', 'Android R',
    _ANDROID_PIXEL4_WEBVIEW_BENCHMARK_CONFIGS, 23, 'android',
    crossbench=_CROSSBENCH_WEBVIEW)
ANDROID_PIXEL4_WEBVIEW_PGO = PerfPlatform(
    'android-pixel4_webview-perf-pgo', 'Android R',
    _ANDROID_PIXEL4_WEBVIEW_BENCHMARK_CONFIGS, 20, 'android',
    crossbench=_CROSSBENCH_WEBVIEW)
ANDROID_PIXEL6 = PerfPlatform('android-pixel6-perf',
                              'Android U',
                              _ANDROID_PIXEL6_BENCHMARK_CONFIGS,
                              14,
                              'android',
                              executables=_ANDROID_DEFAULT_EXECUTABLE_CONFIGS,
                              crossbench=_CROSSBENCH_ANDROID)
ANDROID_PIXEL6_PGO = PerfPlatform(
    'android-pixel6-perf-pgo',
    'Android U',
    _ANDROID_PIXEL6_PGO_BENCHMARK_CONFIGS,
    8,
    'android',
    executables=_ANDROID_DEFAULT_EXECUTABLE_CONFIGS,
    crossbench=_CROSSBENCH_ANDROID)
ANDROID_PIXEL6_PRO = PerfPlatform(
    'android-pixel6-pro-perf',
    'Android T',
    _ANDROID_PIXEL6_PRO_BENCHMARK_CONFIGS,
    10,
    'android',
    executables=_ANDROID_DEFAULT_EXECUTABLE_CONFIGS)
ANDROID_PIXEL6_PRO_PGO = PerfPlatform(
    'android-pixel6-pro-perf-pgo',
    'Android T',
    _ANDROID_PIXEL6_PRO_BENCHMARK_CONFIGS,
    16,
    'android',
    executables=_ANDROID_DEFAULT_EXECUTABLE_CONFIGS,
    pinpoint_only=True)
ANDROID_PIXEL_FOLD = PerfPlatform(
    'android-pixel-fold-perf',
    'Android U',
    _ANDROID_PIXEL_FOLD_BENCHMARK_CONFIGS,
    15,
    'android',
    executables=_ANDROID_DEFAULT_EXECUTABLE_CONFIGS)
ANDROID_PIXEL_TANGOR = PerfPlatform(
    'android-pixel-tangor-perf',
    'Android U',
    _ANDROID_PIXEL_TANGOR_BENCHMARK_CONFIGS,
    8,
    'android',
    executables=_ANDROID_DEFAULT_EXECUTABLE_CONFIGS,
    crossbench=_CROSSBENCH_TANGOR)
ANDROID_GO_WEMBLEY = PerfPlatform('android-go-wembley-perf', 'Android U',
                                  _ANDROID_GO_BENCHMARK_CONFIGS, 15, 'android')
ANDROID_GO_WEMBLEY_WEBVIEW = PerfPlatform(
    'android-go-wembley_webview-perf', 'Android U',
    _ANDROID_GO_WEBVIEW_BENCHMARK_CONFIGS, 20, 'android')
ANDROID_PIXEL9 = PerfPlatform('android-pixel9-perf',
                              'Android B',
                              _ANDROID_PIXEL9_BENCHMARK_CONFIGS,
                              4,
                              'android',
                              executables=_ANDROID_DEFAULT_EXECUTABLE_CONFIGS,
                              crossbench=_CROSSBENCH_PIXEL9)
ANDROID_PIXEL9_PRO = PerfPlatform(
    'android-pixel9-pro-perf',
    'Android B',
    _ANDROID_PIXEL9_BENCHMARK_CONFIGS,
    4,
    'android',
    executables=_ANDROID_DEFAULT_EXECUTABLE_CONFIGS,
    crossbench=_CROSSBENCH_PIXEL9)
ANDROID_PIXEL9_PRO_XL = PerfPlatform(
    'android-pixel9-pro-xl-perf',
    'Android B',
    _ANDROID_PIXEL9_BENCHMARK_CONFIGS,
    4,
    'android',
    executables=_ANDROID_DEFAULT_EXECUTABLE_CONFIGS,
    crossbench=_CROSSBENCH_PIXEL9)
# Cros
FUCHSIA_PERF_NELSON = PerfPlatform('fuchsia-perf-nsn',
                                   '',
                                   _FUCHSIA_PERF_SMARTDISPLAY_BENCHMARK_CONFIGS,
                                   1,
                                   'fuchsia',
                                   is_fyi=True)
FUCHSIA_PERF_SHERLOCK = PerfPlatform(
    'fuchsia-perf-shk',
    '',
    _FUCHSIA_PERF_SMARTDISPLAY_BENCHMARK_CONFIGS,
    1,
    'fuchsia',
    is_fyi=True)

# FYI bots
WIN_10_LOW_END_HP_CANDIDATE = PerfPlatform(
    'win-10_laptop_low_end-perf_HP-Candidate',
    'HP 15-BS121NR Laptop Candidate',
    _WIN_10_LOW_END_HP_CANDIDATE_BENCHMARK_CONFIGS,
    1,
    'win',
    is_fyi=True)
CHROMEOS_KEVIN_PERF_FYI = PerfPlatform('chromeos-kevin-perf-fyi',
                                       '',
                                       _CHROMEOS_KEVIN_FYI_BENCHMARK_CONFIGS,
                                       4,
                                       'chromeos',
                                       is_fyi=True)
LINUX_PERF_FYI = PerfPlatform('linux-perf-fyi',
                              '',
                              _LINUX_PERF_FYI_BENCHMARK_CONFIGS,
                              4,
                              'linux',
                              crossbench=_CROSSBENCH_BENCHMARKS_ALL,
                              is_fyi=True)

# Calibration bots
LINUX_PERF_CALIBRATION = PerfPlatform(
    'linux-perf-calibration',
    'Ubuntu-18.04, 8 core, NVIDIA Quadro P400',
    _LINUX_BENCHMARK_CONFIGS,
    28,
    'linux',
    executables=_LINUX_EXECUTABLE_CONFIGS,
    is_calibration=True)

ALL_PLATFORMS = {
    p for p in locals().values() if isinstance(p, PerfPlatform)
}
PLATFORMS_BY_NAME = {p.name: p for p in ALL_PLATFORMS}
FYI_PLATFORMS = {
    p for p in ALL_PLATFORMS if p.is_fyi
}
CALIBRATION_PLATFORMS = {p for p in ALL_PLATFORMS if p.is_calibration}
OFFICIAL_PLATFORMS = {p for p in ALL_PLATFORMS if p.is_official}
ALL_PLATFORM_NAMES = {
    p.name for p in ALL_PLATFORMS
}
OFFICIAL_PLATFORM_NAMES = {
    p.name for p in OFFICIAL_PLATFORMS
}


def find_bot_platform(builder_name):
  for bot_platform in ALL_PLATFORMS:
    if bot_platform.name == builder_name:
      return bot_platform
  return None
