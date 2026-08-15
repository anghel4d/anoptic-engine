/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0
 * Anoptic targets ISO C++26. */
/*  == Anoptic Game Engine v0.0000001 == */

// Dense mode registry shared by the theory kernel and its public contract.

#ifndef ANO_MUSIC_MODES_H
#define ANO_MUSIC_MODES_H

#include <stdint.h>

#include <anoptic_music.h>

namespace ano {

const char *mode_name(AnoMode mode);
int mode_brightness(AnoMode mode);
const uint8_t *mode_intervals(AnoMode mode);

} // namespace ano

#endif // ANO_MUSIC_MODES_H
