/* SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

namespace cdae {

struct ShapeData;

/** Write ShapeData to a .cdae file. Throws on failure.
 * \param filepath Output file path.
 * \param shape Shape data to write.
 * \param compress Enable zstd compression for body.
 */
bool write_cdae(const char *filepath, const ShapeData &shape, bool compress);

} // namespace cdae
