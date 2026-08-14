/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0
 * Anoptic targets ISO C++26. */

#ifndef ANOPTIC_SCRIPT_H
#define ANOPTIC_SCRIPT_H

struct lua_State;

namespace ano::script {

// Borrowed Lua interpreter state. Lua owns its storage and lifetime.
using State = lua_State;

} // namespace ano::script

extern "C" {

// Lua integration entry point for require("anoptic").
// Borrows state, pushes the Anoptic module table, and returns one result.
[[nodiscard]] int luaopen_anoptic(lua_State* state);

}

#endif /* ANOPTIC_SCRIPT_H */
