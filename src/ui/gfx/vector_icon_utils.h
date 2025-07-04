// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_GFX_VECTOR_ICON_UTILS_H_
#define UI_GFX_VECTOR_ICON_UTILS_H_

#include <vector>

#include "base/component_export.h"
#include "ui/gfx/vector_icon_types.h"

namespace gfx {

// Returns the number of arguments expected to follow |command| in a vector
// icon path representation.
COMPONENT_EXPORT(GFX) int GetCommandArgumentCount(CommandType command);

// Calculates the size that will be default for |icon|, in dip. This will be the
// smallest icon size |icon| contains.
COMPONENT_EXPORT(GFX)
int GetDefaultSizeOfVectorIcon(const VectorIcon& icon);

// Parses into a vector of PathElements per representation.
// NOTE: this should only be used with known/trusted inputs; it will crash if
// parsing fails.
COMPONENT_EXPORT(GFX)
void ParsePathElements(std::string_view s,
                       std::vector<std::vector<PathElement>>& path_elements);

}  // namespace gfx

#endif  // UI_GFX_VECTOR_ICON_UTILS_H_
