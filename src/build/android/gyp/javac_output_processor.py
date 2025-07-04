#!/usr/bin/env python3
#
# Copyright 2021 The Chromium Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Contains helper class for processing javac output."""

import functools
import os
import pathlib
import re
import shlex
import sys
import traceback

from util import build_utils
from util import dep_utils

sys.path.insert(
    0,
    os.path.join(build_utils.DIR_SOURCE_ROOT, 'third_party', 'colorama', 'src'))
import colorama


@functools.lru_cache(maxsize=1)
def _running_locally():
  return os.path.exists('build.ninja')


def yellow(text):
  return colorama.Fore.YELLOW + text + colorama.Fore.RESET


class JavacOutputProcessor:
  def __init__(self, target_name):
    self._target_name = self._RemoveSuffixesIfPresent(
        ["__compile_java", "__errorprone", "__header"], target_name)
    self._missing_classes = set()
    self._suggested_targets = set()
    self._unresolvable_classes = set()

    # Example: ../../ui/android/java/src/org/chromium/ui/base/Clipboard.java:45:
    fileline_prefix = (
        r'(?P<fileline>(?P<file>[-.\w/\\]+.java):(?P<line>[0-9]+):)')

    self._warning_re = re.compile(
        fileline_prefix + r'(?P<full_message> warning: (?P<message>.*))$')
    self._error_re = re.compile(fileline_prefix +
                                r'(?P<full_message> (?P<message>.*))$')
    self._marker_re = re.compile(r'\s*(?P<marker>\^)\s*$')

    self._symbol_not_found_re_list = [
        # Example:
        # error: package org.chromium.components.url_formatter does not exist
        re.compile(fileline_prefix +
                   r'( error: package [\w.]+ does not exist)$'),
        # Example: error: cannot find symbol
        re.compile(fileline_prefix + r'( error: cannot find symbol)$'),
        # Example: error: symbol not found org.chromium.url.GURL
        re.compile(fileline_prefix + r'( error: symbol not found [\w.]+)$'),
    ]

    # Example: import org.chromium.url.GURL;
    self._import_re = re.compile(r'\s*import (?P<imported_class>[\w\.]+);$')
    # Example: import static org.chromium.url.GURL.method;
    self._import_static_re = re.compile(
        r'\s*import static (?P<imported_class>[\w\.]+)\.\s+;$')

    self._warning_color = [
        'full_message', colorama.Fore.YELLOW + colorama.Style.DIM
    ]
    self._error_color = [
        'full_message', colorama.Fore.MAGENTA + colorama.Style.BRIGHT
    ]
    self._marker_color = ['marker', colorama.Fore.BLUE + colorama.Style.BRIGHT]

    self._class_lookup_index = None

    colorama.init()

  def Process(self, lines):
    """ Processes javac output.

      - Applies colors to output.
      - Suggests GN dep to add for 'unresolved symbol in Java import' errors.
      """
    lines = self._ElaborateLinesForUnknownSymbol(iter(lines))
    for line in lines:
      yield self._ApplyColors(line)
    if not self._missing_classes:
      return

    yield yellow('Hint:') + ' One or more errors due to missing GN deps.'
    if self._unresolvable_classes:
      yield 'Failed to find targets for the following classes:'
      for class_name in sorted(self._unresolvable_classes):
        yield f'* {class_name}'
    if self._suggested_targets:
      yield (yellow('Hint:') + ' Try adding the following to ' +
             yellow(self._target_name))

      for targets in sorted(self._suggested_targets):
        if len(targets) > 1:
          suggested_targets_str = 'one of: ' + ', '.join(targets)
        else:
          suggested_targets_str = targets[0]
        # Show them in quotes so they can be copy/pasted into BUILD.gn files.
        yield '    "{}",'.format(suggested_targets_str)

      yield ''
      yield yellow('Hint:') + (' Run the following command to add the missing '
                               'deps:')
      missing_targets = {targets[0] for targets in self._suggested_targets}
      cmd = dep_utils.CreateAddDepsCommand(self._target_name,
                                           sorted(missing_targets))
      yield f'    {shlex.join(cmd)}\n '  # Extra space necessary for new line.
    elif not self._unresolvable_classes:
      yield yellow('Hint:') + ' Rebuild with --offline to show missing deps.'

  def _ElaborateLinesForUnknownSymbol(self, lines):
    """ Elaborates passed-in javac output for unresolved symbols.

    Looks for unresolved symbols in imports.
    Adds:
    - Line with GN target which cannot compile.
    - Mention of unresolved class if not present in error message.
    - Line with suggestion of GN dep to add.

    Args:
      lines: Generator with javac input.
    Returns:
      Generator with processed output.
    """
    previous_line = next(lines, None)
    line = next(lines, None)
    while previous_line is not None:
      try:
        self._LookForUnknownSymbol(previous_line, line)
      except Exception:
        yield 'Error in _LookForUnknownSymbol ---'
        yield from traceback.format_exc().splitlines()
        yield '--- end _LookForUnknownSymbol error'

      yield previous_line
      previous_line = line
      line = next(lines, None)

  def _ApplyColors(self, line):
    """Adds colors to passed-in line and returns processed line."""
    if self._warning_re.match(line):
      line = self._Colorize(line, self._warning_re, self._warning_color)
    elif self._error_re.match(line):
      line = self._Colorize(line, self._error_re, self._error_color)
    elif self._marker_re.match(line):
      line = self._Colorize(line, self._marker_re, self._marker_color)
    return line

  def _LookForUnknownSymbol(self, line, next_line):
    if not next_line:
      return

    import_re_match = self._import_re.match(next_line)
    if not import_re_match:
      import_re_match = self._import_static_re.match(next_line)
      if not import_re_match:
        return

    for regex in self._symbol_not_found_re_list:
      if regex.match(line):
        break
    else:
      return

    class_name = import_re_match.group('imported_class')
    if class_name not in self._missing_classes:
      self._missing_classes.add(class_name)
      if _running_locally():
        self._AnalyzeMissingClass(class_name)

  def _AnalyzeMissingClass(self, class_name):
    if self._class_lookup_index is None:
      self._class_lookup_index = dep_utils.ClassLookupIndex(
          pathlib.Path(os.getcwd()),
          should_build=False,
      )

    suggested_deps = self._class_lookup_index.match(class_name)

    if not suggested_deps:
      self._unresolvable_classes.add(class_name)
      return

    suggested_deps = dep_utils.DisambiguateDeps(suggested_deps)
    self._suggested_targets.add(tuple(d.target for d in suggested_deps))

  @staticmethod
  def _RemoveSuffixesIfPresent(suffixes, text):
    for suffix in suffixes:
      if text.endswith(suffix):
        return text[:-len(suffix)]
    return text

  @staticmethod
  def _Colorize(line, regex, color):
    match = regex.match(line)
    start = match.start(color[0])
    end = match.end(color[0])
    return (line[:start] + color[1] + line[start:end] + colorama.Fore.RESET +
            colorama.Style.RESET_ALL + line[end:])
