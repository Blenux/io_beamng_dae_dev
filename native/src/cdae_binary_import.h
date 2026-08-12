/* SPDX-License-Identifier: GPL-2.0-or-later */

#pragma once

#include <cstdint>
#include <memory>
#include <vector>

namespace cdae {

struct ShapeData;

/** Parse a .cdae file from disk into ShapeData. Throws on failure. */
std::unique_ptr<ShapeData> parse_cdae(const char *filepath);

/** Parse a .cdae from an in-memory byte buffer. Throws on failure. */
std::unique_ptr<ShapeData> parse_cdae_bytes(const uint8_t *data, size_t size);

} // namespace cdae
