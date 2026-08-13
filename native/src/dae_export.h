/* SPDX-License-Identifier: GPL-2.0-or-later */

#pragma once

#include <string>

namespace cdae {

struct ShapeData;

/* Write a ShapeData to a .dae file. Throws std::runtime_error on failure. */
bool dae_write_file(const ShapeData &shape, const char *filepath,
                    const std::string &authoring_tool = "Blender");

} // namespace cdae
