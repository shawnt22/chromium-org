// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/icon_loader.h"

#include "base/notimplemented.h"

// static
IconLoader::IconGroup IconLoader::GroupForFilepath(
    const base::FilePath& file_path) {
  NOTIMPLEMENTED();
  return IconLoader::IconGroup();
}

// static
scoped_refptr<base::TaskRunner> IconLoader::GetReadIconTaskRunner() {
  NOTIMPLEMENTED();
  return nullptr;
}

void IconLoader::ReadIcon() {
  NOTIMPLEMENTED();
  delete this;
}
