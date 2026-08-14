/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0
 * Anoptic targets ISO C++26. */
/*  == Anoptic Game Engine v0.0000001 == */

// Stable-address, hole-reusing storage. std::hive when <hive> exists; else P0447 plf::hive.

#pragma once

#if __has_include(<hive>)
#include <hive>

namespace ano {
using std::hive;
using std::hive_limits;
}
#else
#include <plf_hive.h>

namespace ano {
using plf::hive;
using plf::hive_limits;
}
#endif
