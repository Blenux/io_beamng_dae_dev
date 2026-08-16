/* SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace cdae {

struct ShapeData;

/* Parse a .dae file from disk. Throws std::runtime_error on failure. */
std::unique_ptr<ShapeData> dae_read_file(const char *filepath);

/* Parse .dae from in-memory bytes. Throws std::runtime_error on failure. */
std::unique_ptr<ShapeData> dae_read_from_bytes(const uint8_t *bytes, size_t size);

} // namespace cdae
