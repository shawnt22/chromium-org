# Copyright 2023 The Chromium Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Speedometer 3 Web Interaction Benchmark Pages
"""

import os
import re

from benchmarks import press

from core import path_util

from telemetry import benchmark
from telemetry import story
from telemetry.timeline import chrome_trace_category_filter
from telemetry.web_perf import timeline_based_measurement

from page_sets import speedometer3_pages

_SPEEDOMETER_DIR = os.path.join(path_util.GetChromiumSrcDir(), 'third_party',
                                'speedometer')


class _Speedometer3(press._PressBenchmark):  # pylint: disable=protected-access
  """Abstract base Speedometer 3.x Benchmark class.

  Runs all the speedometer 3 suites by default. Add --suite=<regex> to filter
  out suites, and only run suites whose names are matched by the regular
  expression provided.
  """

  enable_smoke_test_mode = False
  enable_systrace = False
  extra_chrome_categories = False
  enable_rcs = False
  enable_details = False
  iteration_count = None
  take_memory_measurement = False

  @classmethod
  def GetStoryClass(cls):
    raise NotImplementedError()

  def CreateStorySet(self, options):
    should_filter_suites = bool(options.suite)
    story_cls = self.GetStoryClass()
    filtered_suite_names = story_cls.GetSuites(options.suite)

    story_set = story.StorySet(base_dir=self._SOURCE_DIR)

    # For a smoke test one iteration is sufficient
    if self.enable_smoke_test_mode and not self.iteration_count:
      iteration_count = 1
    else:
      iteration_count = self.iteration_count

    story_set.AddStory(
        story_cls(story_set, should_filter_suites, filtered_suite_names,
                  iteration_count, self.enable_details,
                  self.take_memory_measurement))
    return story_set

  def CreateCoreTimelineBasedMeasurementOptions(self):
    if not self.enable_systrace:
      return timeline_based_measurement.Options()

    cat_filter = chrome_trace_category_filter.ChromeTraceCategoryFilter()

    if self.take_memory_measurement:
      cat_filter.AddDisabledByDefault('disabled-by-default-memory-infra')

    # Used for marking ranges in cache_temperature.MarkTelemetryInternal.
    cat_filter.AddIncludedCategory('blink.console')

    # Used to capture TaskQueueManager events.
    cat_filter.AddIncludedCategory('toplevel')

    if self.extra_chrome_categories:
      cat_filter.AddFilterString(self.extra_chrome_categories)

    if self.enable_rcs:
      # V8 needed categories
      cat_filter.AddIncludedCategory('v8')
      cat_filter.AddDisabledByDefault('disabled-by-default-v8.runtime_stats')

      tbm_options = timeline_based_measurement.Options(
          overhead_level=cat_filter)
      tbm_options.SetTimelineBasedMetrics(['runtimeStatsTotalMetric'])
      return tbm_options

    tbm_options = timeline_based_measurement.Options(overhead_level=cat_filter)
    tbm_options.SetTimelineBasedMetrics(['tracingMetric'])
    return tbm_options

  def SetExtraBrowserOptions(self, options):
    if self.enable_rcs:
      options.AppendExtraBrowserArgs(
          '--enable-blink-features=BlinkRuntimeCallStats')

  @classmethod
  def AddBenchmarkCommandLineArgs(cls, parser):
    parser.add_argument('--suite',
                        help='Only runs suites that match regex provided')
    parser.add_argument('--enable-rcs',
                        '--rcs',
                        action='store_true',
                        help='Enables runtime call stats')
    parser.add_argument('--enable-details',
                        '--details',
                        action='store_true',
                        help=('Enables detailed benchmark metrics '
                              '(per line-item, iteration,...)'))
    parser.add_argument('--iteration-count',
                        '--iterations',
                        type=int,
                        help='Override the default number of iterations')

  @classmethod
  def ProcessCommandLineArgs(cls, parser, args):
    if args.suite:
      try:
        if not cls.GetStoryClass().GetSuites(args.suite):
          raise parser.error('--suite: No matches.')
      except re.error:
        raise parser.error('--suite: Invalid regex.')
    if args.enable_systrace or args.enable_rcs:
      cls.enable_systrace = True
    if args.extra_chrome_categories:
      cls.extra_chrome_categories = args.extra_chrome_categories
    if args.enable_rcs:
      cls.enable_rcs = True
    if args.enable_details:
      cls.enable_details = True
    if args.iteration_count:
      cls.iteration_count = args.iteration_count


@benchmark.Info(emails=['cbruni@chromium.org', 'vahl@chromium.org'],
                component='Blink>JavaScript',
                documentation_url='https://github.com/WebKit/Speedometer')
class Speedometer30(_Speedometer3):
  """Speedometer 3.0 benchmark.
  Explicitly named version."""

  SCHEDULED = False
  _SOURCE_DIR = os.path.join(_SPEEDOMETER_DIR, 'v3.0')

  @classmethod
  def GetStoryClass(cls):
    return speedometer3_pages.Speedometer30Story

  @classmethod
  def Name(cls):
    return 'speedometer3.0'


@benchmark.Info(emails=['cbruni@chromium.org', 'vahl@chromium.org'],
                component='Blink>JavaScript',
                documentation_url='https://github.com/WebKit/Speedometer')
class Speedometer31(_Speedometer3):
  """Speedometer 3.1 benchmark.
  Explicitly named version."""

  SCHEDULED = False
  _SOURCE_DIR = os.path.join(_SPEEDOMETER_DIR, 'v3.1')

  @classmethod
  def GetStoryClass(cls):
    return speedometer3_pages.Speedometer31Story

  @classmethod
  def Name(cls):
    return 'speedometer3.1'


@benchmark.Info(emails=['cbruni@chromium.org', 'vahl@chromium.org'],
                component='Blink>JavaScript',
                documentation_url='https://github.com/WebKit/Speedometer')
class Speedometer3(Speedometer31):
  """The latest version of the Speedometer 3.x benchmark."""
  SCHEDULED = True

  @classmethod
  def GetStoryClass(cls):
    return speedometer3_pages.Speedometer3Story

  @classmethod
  def Name(cls):
    return 'speedometer3'


@benchmark.Info(emails=['cbruni@chromium.org', 'vahl@chromium.org'],
                component='Blink>JavaScript',
                documentation_url='https://github.com/WebKit/Speedometer')
class Speedometer3Future(Speedometer3):
  """The latest Speedometer 3.x benchmark with the V8 flag --future.

  Shows the performance of upcoming V8 VM features.
  """
  @classmethod
  def Name(cls):
    return 'speedometer3-future'

  def SetExtraBrowserOptions(self, options):
    options.AppendExtraBrowserArgs('--enable-features=V8VmFuture')


@benchmark.Info(emails=['omerkatz@chromium.org'],
                component='Blink>JavaScript>GarbageCollection',
                documentation_url='https://github.com/WebKit/Speedometer')
class Speedometer3MinorMS(Speedometer3):
  """The latest Speedometer 3.x benchmark without the MinorMS flag.

  Shows the performance of Scavenger young generation GC in V8.
  """
  @classmethod
  def Name(cls):
    return 'speedometer3-minorms'

  def SetExtraBrowserOptions(self, options):
    options.AppendExtraBrowserArgs('--js-flags=--minor-ms')


@benchmark.Info(emails=['rasikan@google.com', 'wnwen@google.com'],
                component='Blink>JavaScript',
                documentation_url='https://github.com/WebKit/Speedometer')
class Speedometer3Predictable(Speedometer3):
  """The latest Speedometer 3.x benchmark with V8's `predictable` mode.

  This should (hopefully) help reduce variance in the score.
  """

  @classmethod
  def Name(cls):
    return 'speedometer3-predictable'

  def SetExtraBrowserOptions(self, options):
    options.AppendExtraBrowserArgs('--js-flags=--predictable')


@benchmark.Info(emails=['cbruni@chromium.org', 'vahl@chromium.org'],
                component='Blink>JavaScript',
                documentation_url='https://github.com/WebKit/Speedometer')
class Speedometer3NoFieldTrials(Speedometer3):
  """The latest Speedometer 3.x benchmark without field-trials.
  """

  SCHEDULED = False

  @classmethod
  def Name(cls):
    return 'speedometer3-no-field-trials'

  def SetExtraBrowserOptions(self, options):
    options.AppendExtraBrowserArgs('--disable-field-trial-config')
    options.RemoveExtraBrowserArg('--enable-field-trial-config')
