#!/usr/bin/env python3
#
# Copyright 2016 The Chromium Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Tool that combines a sequence of input proguard files and outputs a single
# proguard file.
#
# The final output file is formed by concatenating all of the
# input proguard files.


import argparse
import pathlib
import os
import re
import sys
import json
REPOSITORY_ROOT = os.path.abspath(
    os.path.join(os.path.dirname(__file__), os.pardir, os.pardir, os.pardir))

sys.path.insert(0, os.path.join(REPOSITORY_ROOT, 'build'))
import action_helpers  # pylint: disable=wrong-import-position

def _ReadFile(path):
  """Reads a file as a string."""
  return pathlib.Path(path).read_text()


def ReadProguardConfigsPath(build_config_path):
  """
  Reads the transitive proguard configs path of target whose build
  config path is `build_config_path`

  @param path: The path to the initial build_config
  @returns A set of the proguard config paths found during traversal.
  """
  proguard_configs_path = set()
  with open(build_config_path, 'r') as f:
    build_config = json.load(f)
    proguard_configs_path.update(build_config.get("proguard_all_configs",
                                                  set()))

  params_path = build_config_path.replace('.build_config.json', '.params.json')
  with open(params_path, 'r') as f:
    params_json = json.load(f)
    # java_library targets don't have `proguard_all_configs` so we need
    # to look at `proguard_configs` field instead.
    proguard_configs_path.update(params_json.get("proguard_configs", set()))
  return proguard_configs_path


def _post_process_concatenated_rules_for_aosp(rules: str) -> str:
  """Post-process the concatenated rules to match the packages in AOSP. Cronet
  in AOSP is repackaged where all packages are prefixed with `android.net.connectivity`.
  So does method will append ** to every package name found.

  Args:
    rules: Rules before processing
  """
  for package_name in ("org.chromium", "com.google.protobuf", "org.jni_zero",
                       "androidx.annotation"):
    # This regex will match anything substring that matches one of the package
    # names but is not preceded by either '*' or '/'. The latter prevents
    # comments containing these packages names from being modified. Example:
    # -------- Config Path: androidx.annotation/proguard.txt --------
    rules = re.sub(f"([^\*\/]){package_name}",
                   f"\g<1>android.net.connectivity.{package_name}", rules)
  return rules

def main():
  parser = argparse.ArgumentParser()
  parser.add_argument('--output_file',
          help='Output file for the generated proguard file')
  parser.add_argument(
      '--aosp-mode',
      help=
      'This will apply additional processing for the combined rules to ensure they match HttpEngine (Cronet In AOSP).',
      action='store_true')
  parser.add_argument('--dep_file',
                      help='Depfile path to write the implicit inputs')

  args, input_files = parser.parse_known_args()
  # Fetch all proguard configs
  all_proguard_configs_path = set()
  for input_file in input_files:
    all_proguard_configs_path.update(ReadProguardConfigsPath(input_file))
  str_output = ""
  # Concatenate all proguard rules and sort to maintain deterministic output.
  for proguard_config_path in sorted(all_proguard_configs_path):
    str_output += f"# -------- Config Path: {proguard_config_path.replace('../', '')} --------\n"
    str_output += _ReadFile(proguard_config_path)
  if args.aosp_mode:
    str_output = _post_process_concatenated_rules_for_aosp(str_output)
  with open(args.output_file, 'w') as target:
    target.write(str_output)
  action_helpers.write_depfile(
      args.dep_file, args.output_file, all_proguard_configs_path)



if __name__ == '__main__':
  sys.exit(main())
