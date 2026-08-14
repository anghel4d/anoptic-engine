/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0
 * Anoptic targets ISO C++26. */

#pragma once

struct lua_State;

namespace ano::script {

// Borrowed Lua state for the eventual generated integration boundary.
using State = ::lua_State;

} // namespace ano::script
