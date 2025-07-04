#!/usr/bin/env python3
# Copyright 2020 The Chromium Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Builds and runs a test by filename.

This script finds the appropriate test suites for the specified test files or
directories, builds it, then runs it with the (optionally) specified filter,
passing any extra args on to the test runner.

Examples:
# Run the test target for bit_cast_unittest.cc. Use a custom test filter instead
# of the automatically generated one.
autotest.py -C out/Desktop bit_cast_unittest.cc --gtest_filter=BitCastTest*

# Find and run UrlUtilitiesUnitTest.java's tests, pass remaining parameters to
# the test binary.
autotest.py -C out/Android UrlUtilitiesUnitTest --fast-local-dev -v

# Run all tests under base/strings.
autotest.py -C out/foo --run-all base/strings

# Run tests in multiple files or directories.
autotest.py -C out/foo base/strings base/pickle_unittest.cc

# Run only the test on line 11. Useful when running autotest.py from your text
# editor.
autotest.py -C out/foo --line 11 base/strings/strcat_unittest.cc
"""

import argparse
import json
import locale
import os
import re
import shlex
import subprocess
import sys
import shutil

from enum import Enum
from pathlib import Path

# Don't write pyc files to the src tree, which show up in version control
# in some environments.
sys.dont_write_bytecode = True

USE_PYTHON_3 = f'This script will only run under python3.'

SRC_DIR = Path(__file__).parent.parent.resolve()
sys.path.append(str(SRC_DIR / 'build'))
import gn_helpers

sys.path.append(str(SRC_DIR / 'build' / 'android'))
from pylib import constants

DEPOT_TOOLS_DIR = SRC_DIR / 'third_party' / 'depot_tools'
DEBUG = False

# Some test suites use suffixes that would also match non-test-suite targets.
# Those test suites should be manually added here.
_TEST_TARGET_ALLOWLIST = [

    # The tests below this line were output from the ripgrep command just below:
    '//ash:ash_pixeltests',
    '//build/rust/tests/test_serde_json_lenient:test_serde_json_lenient',
    '//chrome/browser/apps/app_service/app_install:app_install_fuzztests',
    '//chrome/browser/glic/e2e_test:glic_internal_e2e_interactive_ui_tests',
    '//chrome/browser/mac:install_sh_test',
    '//chrome/browser/metrics/perf:profile_provider_unittest',
    '//chrome/browser/privacy_sandbox/notice:fuzz_tests',
    '//chrome/browser/web_applications:web_application_fuzztests',
    '//chromecast/media/base:video_plane_controller_test',
    '//chromecast/metrics:cast_metrics_unittest',
    '//chrome/enterprise_companion:enterprise_companion_integration_tests',
    '//chrome/enterprise_companion:enterprise_companion_tests',
    '//chrome/installer/gcapi:gcapi_test',
    '//chrome/installer/test:upgrade_test',
    '//chromeos/ash/components/kiosk/vision:kiosk_vision_unit_tests',
    '//chrome/test/android:chrome_public_apk_baseline_profile_generator',
    '//chrome/test:unit_tests',
    '//clank/javatests:chrome_apk_baseline_profile_generator',
    '//clank/javatests:chrome_smoke_test',
    '//clank/javatests:monochrome_bundle_smoke_test',
    '//clank/javatests:trichrome_chrome_google_bundle_smoke_test',
    '//components/chromeos_camera:jpeg_decode_accelerator_unittest',
    '//components/exo/wayland:wayland_client_compatibility_tests',
    '//components/exo/wayland:wayland_client_tests',
    '//components/facilitated_payments/core/validation:pix_code_validator_fuzzer',
    '//components/ip_protection:components_ip_protection_fuzztests',
    '//components/minidump_uploader:minidump_uploader_test',
    '//components/paint_preview/browser:paint_preview_browser_unit_tests',
    '//components/paint_preview/common:paint_preview_common_unit_tests',
    '//components/paint_preview/renderer:paint_preview_renderer_unit_tests',
    '//components/services/paint_preview_compositor:paint_preview_compositor_unit_tests',
    '//components/translate/core/language_detection:language_detection_util_fuzztest',
    '//components/webcrypto:webcrypto_testing_fuzzer',
    '//components/zucchini:zucchini_integration_test',
    '//content/test/fuzzer:devtools_protocol_encoding_json_fuzzer',
    '//fuchsia_web/runners:cast_runner_integration_tests',
    '//fuchsia_web/webengine:web_engine_integration_tests',
    '//google_apis/gcm:gcm_unit_tests',
    '//gpu:gl_tests',
    '//gpu:gpu_benchmark',
    '//gpu/vulkan/android:vk_tests',
    '//ios/web:ios_web_inttests',
    '//ios/web_view:ios_web_view_inttests',
    '//media/cdm:aes_decryptor_fuzztests',
    '//media/formats:ac3_util_fuzzer',
    '//media/gpu/chromeos:image_processor_test',
    '//media/gpu/v4l2:v4l2_unittest',
    '//media/gpu/vaapi/test/fake_libva_driver:fake_libva_driver_unittest',
    '//media/gpu/vaapi:vaapi_unittest',
    '//native_client/tests:large_tests',
    '//native_client/tests:medium_tests',
    '//native_client/tests:small_tests',
    '//sandbox/mac:sandbox_mac_fuzztests',
    '//sandbox/win:sbox_integration_tests',
    '//sandbox/win:sbox_validation_tests',
    '//testing/libfuzzer/fuzzers:libyuv_scale_fuzztest',
    '//testing/libfuzzer/fuzzers:paint_vector_icon_fuzztest',
    '//third_party/blink/renderer/controller:blink_perf_tests',
    '//third_party/blink/renderer/core:css_parser_fuzzer',
    '//third_party/blink/renderer/core:inspector_ghost_rules_fuzzer',
    '//third_party/blink/renderer/platform/loader:unencoded_digest_fuzzer',
    '//third_party/crc32c:crc32c_benchmark',
    '//third_party/crc32c:crc32c_tests',
    '//third_party/dawn/src/dawn/tests/benchmarks:dawn_benchmarks',
    '//third_party/highway:highway_tests',
    '//third_party/ipcz/src:ipcz_tests',
    '//third_party/libaom:av1_encoder_fuzz_test',
    '//third_party/libaom:test_libaom',
    '//third_party/libvpx:test_libvpx',
    '//third_party/libvpx:vp8_encoder_fuzz_test',
    '//third_party/libvpx:vp9_encoder_fuzz_test',
    '//third_party/libwebp:libwebp_advanced_api_fuzzer',
    '//third_party/libwebp:libwebp_animation_api_fuzzer',
    '//third_party/libwebp:libwebp_animencoder_fuzzer',
    '//third_party/libwebp:libwebp_enc_dec_api_fuzzer',
    '//third_party/libwebp:libwebp_huffman_fuzzer',
    '//third_party/libwebp:libwebp_mux_demux_api_fuzzer',
    '//third_party/libwebp:libwebp_simple_api_fuzzer',
    '//third_party/opus:test_opus_api',
    '//third_party/opus:test_opus_decode',
    '//third_party/opus:test_opus_encode',
    '//third_party/opus:test_opus_padding',
    '//third_party/pdfium:pdfium_embeddertests',
    '//third_party/pffft:pffft_unittest',
    '//third_party/rapidhash:rapidhash_fuzztests',
    '//ui/ozone:ozone_integration_tests',
]
r"""
 You can run this command to find test targets that do not match these regexes,
 and use it to update _TEST_TARGET_ALLOWLIST.
rg '^(instrumentation_test_runner|test)\("([^"]*)' -o -g'BUILD.gn' -r'$2' -N \
  | rg -v '(_browsertests|_perftests|_wpr_tests|_unittests)$' \
  | rg '^(.*)/BUILD.gn(.*)$' -r'\'//$1$2\',' \
  | sort

 And you can use a command like this to find source_set targets that do match
 the test target regex (ideally this is minimal).
rg '^source_set\("([^"]*)' -o -g'BUILD.gn' -r'$1' -N | \
  rg '(_browsertests|_perftests|_wpr_tests|_unittests)$'
"""
_TEST_TARGET_REGEX = re.compile(
    r'(_browsertests|_perftests|_wpr_tests|_unittests)$')

_PREF_MAPPING_FILE_PATTERN = re.escape(
    str(Path('components') / 'policy' / 'test' / 'data' / 'pref_mapping') +
    r'/') + r'.*\.json'

TEST_FILE_NAME_REGEX = re.compile(
    r'(.*Test\.java)' +
    r'|(.*_[a-z]*test(?:_win|_mac|_linux|_chromeos|_android)?\.cc)' + r'|(' +
    _PREF_MAPPING_FILE_PATTERN + r')')

# Some tests don't directly include gtest.h and instead include it via gmock.h
# or a test_utils.h file, so make sure these cases are captured. Also include
# files that use <...> for #includes instead of quotes.
GTEST_INCLUDE_REGEX = re.compile(
    r'#include.*(gtest|gmock|_test_utils|browser_test)\.h("|>)')


def ExitWithMessage(*args):
  print(*args, file=sys.stderr)
  sys.exit(1)


class TestValidity(Enum):
  NOT_A_TEST = 0  # Does not match test file regex.
  MAYBE_A_TEST = 1  # Matches test file regex, but doesn't include gtest files.
  VALID_TEST = 2  # Matches test file regex and includes gtest files.


def FindRemoteCandidates(target):
  """Find files using a remote code search utility, if installed."""
  if not shutil.which('cs'):
    return []
  results = RunCommand([
      'cs', '-l',
      # Give the local path to the file, if the file exists.
      '--local',
      f'file:{target}',
      # Restrict our search to Chromium
      'git:chrome-internal/codesearch/chrome/src@main']).splitlines()
  exact = set()
  close = set()
  for filename in results:
    file_validity = IsTestFile(filename)
    if file_validity is TestValidity.VALID_TEST:
      exact.add(filename)
    elif file_validity is TestValidity.MAYBE_A_TEST:
      close.add(filename)
  return list(exact), list(close)


def IsTestFile(file_path):
  if not TEST_FILE_NAME_REGEX.match(file_path):
    return TestValidity.NOT_A_TEST
  if file_path.endswith('.cc'):
    # Try a bit harder to remove non-test files for c++. Without this,
    # 'autotest.py base/' finds non-test files.
    try:
      with open(file_path, 'r', encoding='utf-8') as f:
        if GTEST_INCLUDE_REGEX.search(f.read()) is not None:
          return TestValidity.VALID_TEST
    except IOError:
      pass
    # It may still be a test file, even if it doesn't include a gtest file.
    return TestValidity.MAYBE_A_TEST
  return TestValidity.VALID_TEST


class CommandError(Exception):
  """Exception thrown when a subcommand fails."""

  def __init__(self, command, return_code, output=None):
    Exception.__init__(self)
    self.command = command
    self.return_code = return_code
    self.output = output

  def __str__(self):
    message = (f'\n***\nERROR: Error while running command {self.command}'
               f'.\nExit status: {self.return_code}\n')
    if self.output:
      message += f'Output:\n{self.output}\n'
    message += '***'
    return message


def StreamCommandOrExit(cmd, **kwargs):
  try:
    subprocess.check_call(cmd, **kwargs)
  except subprocess.CalledProcessError as e:
    sys.exit(1)


def RunCommand(cmd, **kwargs):
  try:
    # Set an encoding to convert the binary output to a string.
    return subprocess.check_output(
        cmd, **kwargs, encoding=locale.getpreferredencoding())
  except subprocess.CalledProcessError as e:
    raise CommandError(e.cmd, e.returncode, e.output) from None


def BuildTestTargets(out_dir, targets, dry_run, quiet):
  """Builds the specified targets with ninja"""
  cmd = gn_helpers.CreateBuildCommand(out_dir) + targets
  print('Building: ' + shlex.join(cmd))
  if (dry_run):
    return True
  completed_process = subprocess.run(cmd,
                                     capture_output=quiet,
                                     encoding='utf-8')
  if completed_process.returncode != 0:
    if quiet:
      before, _, after = completed_process.stdout.partition('stderr:')
      if not after:
        before, _, after = completed_process.stdout.partition('stdout:')
      if after:
        print(after)
      else:
        print(before)
    return False
  return True


def RecursiveMatchFilename(folder, filename):
  current_dir = os.path.split(folder)[-1]
  if current_dir.startswith('out') or current_dir.startswith('.'):
    return [[], []]
  exact = []
  close = []
  try:
    with os.scandir(folder) as it:
      for entry in it:
        if (entry.is_symlink()):
          continue
        if (entry.is_file() and filename in entry.path and
            not os.path.basename(entry.path).startswith('.')):
          file_validity = IsTestFile(entry.path)
          if file_validity is TestValidity.VALID_TEST:
            exact.append(entry.path)
          elif file_validity is TestValidity.MAYBE_A_TEST:
            close.append(entry.path)
        if entry.is_dir():
          # On Windows, junctions are like a symlink that python interprets as a
          # directory, leading to exceptions being thrown. We can just catch and
          # ignore these exceptions like we would ignore symlinks.
          try:
            matches = RecursiveMatchFilename(entry.path, filename)
            exact += matches[0]
            close += matches[1]
          except FileNotFoundError as e:
            if DEBUG:
              print(f'Failed to scan directory "{entry}" - junction?')
            pass
  except PermissionError:
    print(f'Permission error while scanning {folder}')

  return [exact, close]


def FindTestFilesInDirectory(directory):
  test_files = []
  if DEBUG:
    print('Test files:')
  for root, _, files in os.walk(directory):
    for f in files:
      path = os.path.join(root, f)
      file_validity = IsTestFile(path)
      if file_validity is TestValidity.VALID_TEST:
        if DEBUG:
          print(path)
        test_files.append(path)
      elif DEBUG and file_validity is TestValidity.MAYBE_A_TEST:
        print(path + ' matched but doesn\'t include gtest files, skipping.')
  return test_files


def FindMatchingTestFiles(target, remote_search=False):
  # Return early if there's an exact file match.
  if os.path.isfile(target):
    # If the target is a C++ implementation file, try to guess the test file.
    if target.endswith('.cc') or target.endswith('.h'):
      target_validity = IsTestFile(target)
      if target_validity is TestValidity.VALID_TEST:
        return [target]
      alternate = f"{target.rsplit('.', 1)[0]}_unittest.cc"
      alt_validity = TestValidity.NOT_A_TEST if not os.path.isfile(
          alternate) else IsTestFile(alternate)
      if alt_validity is TestValidity.VALID_TEST:
        return [alternate]

      # If neither the target nor its alternative were valid, check if they just
      # didn't include the gtest files before deciding to exit.
      if target_validity is TestValidity.MAYBE_A_TEST:
        return [target]
      if alt_validity is TestValidity.MAYBE_A_TEST:
        return [alternate]
      ExitWithMessage(f"{target} doesn't look like a test file")
    return [target]
  # If this is a directory, return all the test files it contains.
  if os.path.isdir(target):
    files = FindTestFilesInDirectory(target)
    if not files:
      ExitWithMessage('No tests found in directory')
    return files

  if sys.platform.startswith('win32') and os.path.altsep in target:
    # Use backslash as the path separator on Windows to match os.scandir().
    if DEBUG:
      print('Replacing ' + os.path.altsep + ' with ' + os.path.sep + ' in: '
            + target)
    target = target.replace(os.path.altsep, os.path.sep)
  if DEBUG:
    print('Finding files with full path containing: ' + target)

  if remote_search:
    exact, close = FindRemoteCandidates(target)
    if not exact and not close:
      print('Failed to find remote candidates; searching recursively')
      exact, close = RecursiveMatchFilename(SRC_DIR, target)
  else:
    exact, close = RecursiveMatchFilename(SRC_DIR, target)

  if DEBUG:
    if exact:
      print('Found exact matching file(s):')
      print('\n'.join(exact))
    if close:
      print('Found possible matching file(s):')
      print('\n'.join(close))

  if len(exact) >= 1:
    # Given "Foo", don't ask to disambiguate ModFoo.java vs Foo.java.
    more_exact = [
        p for p in exact if os.path.basename(p) in (target, f'{target}.java')
    ]
    if len(more_exact) == 1:
      test_files = more_exact
    else:
      test_files = exact
  else:
    test_files = close

  if len(test_files) > 1:
    if len(test_files) < 10:
      test_files = [HaveUserPickFile(test_files)]
    else:
      # Arbitrarily capping at 10 results so we don't print the name of every
      # file in the repo if the target is poorly specified.
      test_files = test_files[:10]
      ExitWithMessage(f'Target "{target}" is ambiguous. Matching files: '
                      f'{test_files}')
  if not test_files:
    ExitWithMessage(f'Target "{target}" did not match any files.')
  return test_files


def HaveUserPickFile(paths):
  paths = sorted(paths, key=lambda p: (len(p), p))
  path_list = '\n'.join(f'{i}. {t}' for i, t in enumerate(paths))

  while True:
    user_input = input(f'Please choose the path you mean.\n{path_list}\n')
    try:
      value = int(user_input)
      return paths[value]
    except (ValueError, IndexError):
      print('Try again')


def HaveUserPickTarget(paths, targets):
  # Cap to 10 targets for convenience [0-9].
  targets = targets[:10]
  target_list = '\n'.join(f'{i}. {t}' for i, t in enumerate(targets))

  user_input = input(f'Target "{paths}" is used by multiple test targets.\n' +
                     target_list + '\nPlease pick a target by its numeric index'
                     'listed below: ')
  try:
    value = int(user_input)
    return targets[value]
  except (ValueError, IndexError):
    print('Value entered was not a numeric index listed above. Trying again.')
    return HaveUserPickTarget(paths, targets)


# A persistent cache to avoid running gn on repeated runs of autotest.
class TargetCache:
  def __init__(self, out_dir):
    self.out_dir = out_dir
    self.path = os.path.join(out_dir, 'autotest_cache')
    self.gold_mtime = self.GetBuildNinjaMtime()
    self.cache = {}
    try:
      mtime, cache = json.load(open(self.path, 'r'))
      if mtime == self.gold_mtime:
        self.cache = cache
    except Exception:
      pass

  def Save(self):
    with open(self.path, 'w') as f:
      json.dump([self.gold_mtime, self.cache], f)

  def Find(self, test_paths):
    key = ' '.join(test_paths)
    return self.cache.get(key, None)

  def Store(self, test_paths, test_targets):
    key = ' '.join(test_paths)
    self.cache[key] = test_targets

  def GetBuildNinjaMtime(self):
    return os.path.getmtime(os.path.join(self.out_dir, 'build.ninja'))

  def IsStillValid(self):
    return self.GetBuildNinjaMtime() == self.gold_mtime


def _TestTargetsFromGnRefs(targets):
  # First apply allowlists:
  ret = [t for t in targets if '__' not in t]
  ret = [
      t for t in ret
      if _TEST_TARGET_REGEX.search(t) or t in _TEST_TARGET_ALLOWLIST
  ]
  if ret:
    return ret

  _SUBTARGET_SUFFIXES = (
      '__java_binary',  # robolectric_binary()
      '__test_runner_script',  # test() targets
      '__test_apk',  # instrumentation_test_apk() targets
  )
  ret = []
  for suffix in _SUBTARGET_SUFFIXES:
    ret.extend(t[:-len(suffix)] for t in targets if t.endswith(suffix))

  return ret


def FindTestTargets(target_cache, out_dir, paths, run_all):
  # Normalize paths, so they can be cached.
  paths = [os.path.realpath(p) for p in paths]
  test_targets = target_cache.Find(paths)
  used_cache = True
  if not test_targets:
    used_cache = False

    # Use gn refs to recursively find all targets that depend on |path|, filter
    # internal gn targets, and match against well-known test suffixes, falling
    # back to a list of known test targets if that fails.
    gn_path = os.path.join(DEPOT_TOOLS_DIR, 'gn.py')

    cmd = [sys.executable, gn_path, 'refs', out_dir, '--all'] + paths
    targets = RunCommand(cmd).splitlines()
    test_targets = _TestTargetsFromGnRefs(targets)

    # If not targets were identified as tests by looking at their names, ask GN
    # if any are executables.
    if not test_targets and targets:
      test_targets = RunCommand(cmd + ['--type=executable']).splitlines()

  if not test_targets:
    ExitWithMessage(
        f'"{paths}" did not match any test targets. Consider adding'
        f' one of the following targets to _TEST_TARGET_ALLOWLIST within '
        f'{__file__}: \n' + '\n'.join(targets))

  test_targets.sort()
  target_cache.Store(paths, test_targets)
  target_cache.Save()

  if len(test_targets) > 1:
    if run_all:
      print(f'Warning, found {len(test_targets)} test targets.',
            file=sys.stderr)
      if len(test_targets) > 10:
        ExitWithMessage('Your query likely involves non-test sources.')
      print('Trying to run all of them!', file=sys.stderr)
    else:
      test_targets = [HaveUserPickTarget(paths, test_targets)]

  # Remove the // prefix to turn GN label into ninja target.
  test_targets = [t[2:] for t in test_targets]

  return (test_targets, used_cache)


def RunTestTargets(out_dir, targets, gtest_filter, pref_mapping_filter,
                   extra_args, dry_run, no_try_android_wrappers,
                   no_fast_local_dev):

  for target in targets:
    target_binary = target.split(':')[1]

    # Look for the Android wrapper script first.
    path = os.path.join(out_dir, 'bin', f'run_{target_binary}')
    if no_try_android_wrappers or not os.path.isfile(path):
      # If the wrapper is not found or disabled use the Desktop target
      # which is an executable.
      path = os.path.join(out_dir, target_binary)
    elif not no_fast_local_dev:
      # Usually want this flag when developing locally.
      extra_args = extra_args + ['--fast-local-dev']

    cmd = [path, f'--gtest_filter={gtest_filter}']
    if pref_mapping_filter:
      cmd.append(f'--test_policy_to_pref_mappings_filter={pref_mapping_filter}')
    cmd.extend(extra_args)

    print('Running test: ' + shlex.join(cmd))
    if not dry_run:
      StreamCommandOrExit(cmd)


def BuildCppTestFilter(filenames, line):
  make_filter_command = [
      sys.executable, SRC_DIR / 'tools' / 'make_gtest_filter.py'
  ]
  if line:
    make_filter_command += ['--line', str(line)]
  else:
    make_filter_command += ['--class-only']
  make_filter_command += filenames
  return RunCommand(make_filter_command).strip()


def BuildJavaTestFilter(filenames):
  return ':'.join('*.{}*'.format(os.path.splitext(os.path.basename(f))[0])
                  for f in filenames)


_PREF_MAPPING_GTEST_FILTER = '*PolicyPrefsTest.PolicyToPrefsMapping*'

_PREF_MAPPING_FILE_REGEX = re.compile(_PREF_MAPPING_FILE_PATTERN)

SPECIAL_TEST_FILTERS = [(_PREF_MAPPING_FILE_REGEX, _PREF_MAPPING_GTEST_FILTER)]


def BuildTestFilter(filenames, line):
  java_files = [f for f in filenames if f.endswith('.java')]
  cc_files = [f for f in filenames if f.endswith('.cc')]
  filters = []
  if java_files:
    filters.append(BuildJavaTestFilter(java_files))
  if cc_files:
    filters.append(BuildCppTestFilter(cc_files, line))
  for regex, gtest_filter in SPECIAL_TEST_FILTERS:
    if any(True for f in filenames if regex.match(f)):
      filters.append(gtest_filter)
      break
  return ':'.join(filters)


def BuildPrefMappingTestFilter(filenames):
  mapping_files = [f for f in filenames if _PREF_MAPPING_FILE_REGEX.match(f)]
  if not mapping_files:
    return None
  names_without_extension = [Path(f).stem for f in mapping_files]
  return ':'.join(names_without_extension)


def main():
  parser = argparse.ArgumentParser(
      description=__doc__, formatter_class=argparse.RawTextHelpFormatter)
  parser.add_argument('--out-dir',
                      '--out_dir',
                      '--output-directory',
                      '--output_directory',
                      '-C',
                      metavar='OUT_DIR',
                      help='output directory of the build')
  parser.add_argument('--remote-search',
                      '--remote_search',
                      '-r',
                      action='store_true',
                      help='Search for tests using a remote service')
  parser.add_argument(
      '--run-all',
      '--run_all',
      action='store_true',
      help='Run all tests for the file or directory, instead of just one')
  parser.add_argument('--line',
                      type=int,
                      help='run only the test on this line number. c++ only.')
  parser.add_argument('--gtest-filter',
                      '--gtest_filter',
                      '-f',
                      metavar='FILTER',
                      help='test filter')
  parser.add_argument('--test-policy-to-pref-mappings-filter',
                      '--test_policy_to_pref_mappings_filter',
                      metavar='FILTER',
                      help='policy pref mappings test filter')
  parser.add_argument(
      '--dry-run',
      '--dry_run',
      '-n',
      action='store_true',
      help='Print ninja and test run commands without executing them.')
  parser.add_argument(
      '--quiet',
      '-q',
      action='store_true',
      help='Do not print while building, only print if build fails.')
  parser.add_argument(
      '--no-try-android-wrappers',
      '--no_try_android_wrappers',
      action='store_true',
      help='Do not try to use Android test wrappers to run tests.')
  parser.add_argument('--no-fast-local-dev',
                      '--no_fast_local_dev',
                      action='store_true',
                      help='Do not add --fast-local-dev for Android tests.')
  parser.add_argument('files',
                      metavar='FILE_NAME',
                      nargs='+',
                      help='test suite file (eg. FooTest.java)')

  args, _extras = parser.parse_known_args()

  if args.out_dir:
    constants.SetOutputDirectory(args.out_dir)
  constants.CheckOutputDirectory()
  out_dir: str = constants.GetOutDirectory()

  if not os.path.isdir(out_dir):
    parser.error(f'OUT_DIR "{out_dir}" does not exist.')
  target_cache = TargetCache(out_dir)
  filenames = []
  for file in args.files:
    filenames.extend(FindMatchingTestFiles(file, args.remote_search))

  targets, used_cache = FindTestTargets(target_cache, out_dir, filenames,
                                        args.run_all)

  gtest_filter = args.gtest_filter
  if not gtest_filter:
    gtest_filter = BuildTestFilter(filenames, args.line)

  if not gtest_filter:
    ExitWithMessage('Failed to derive a gtest filter')

  pref_mapping_filter = args.test_policy_to_pref_mappings_filter
  if not pref_mapping_filter:
    pref_mapping_filter = BuildPrefMappingTestFilter(filenames)

  assert targets
  build_ok = BuildTestTargets(out_dir, targets, args.dry_run, args.quiet)

  # If we used the target cache, it's possible we chose the wrong target because
  # a gn file was changed. The build step above will check for gn modifications
  # and update build.ninja. Use this opportunity the verify the cache is still
  # valid.
  if used_cache and not target_cache.IsStillValid():
    target_cache = TargetCache(out_dir)
    new_targets, _ = FindTestTargets(target_cache, out_dir, filenames,
                                     args.run_all)
    if targets != new_targets:
      # Note that this can happen, for example, if you rename a test target.
      print('gn config was changed, trying to build again', file=sys.stderr)
      targets = new_targets
      build_ok = BuildTestTargets(out_dir, targets, args.dry_run, args.quiet)

  if not build_ok: sys.exit(1)

  RunTestTargets(out_dir, targets, gtest_filter, pref_mapping_filter, _extras,
                 args.dry_run, args.no_try_android_wrappers,
                 args.no_fast_local_dev)


if __name__ == '__main__':
  sys.exit(main())
