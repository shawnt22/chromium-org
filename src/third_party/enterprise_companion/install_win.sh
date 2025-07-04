#!/bin/bash
# Copyright 2025 The Chromium Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set -e
set -x
set -o pipefail

# An auto-created directory whose content will ultimately be uploaded to CIPD.
# The commands below should output the built product to this directory.
PREFIX="$1"

mv enterprise_companion_test.exe "$PREFIX"
if [ -f icudtl.dat ]; then
  mv icudtl.dat "$PREFIX"
fi