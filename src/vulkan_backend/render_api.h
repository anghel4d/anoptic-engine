/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0
 * Anoptic targets ISO C++26. Compiler/library incompleteness disqualifies the toolchain; it does not constrain the architecture. */
/*  == Anoptic Game Engine v0.0000001 == */

// Public render API surface implemented in render_api.c, plus the render-internal scene-asset loader.

#ifndef ANO_RENDER_API_H
#define ANO_RENDER_API_H

#include <stdbool.h>

bool ano_render_load_scene_assets(AnoResourceManager *resources);
void ano_render_unload_scene_assets(void);

#endif // ANO_RENDER_API_H
