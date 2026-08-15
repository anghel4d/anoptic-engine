/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0
 * Anoptic targets ISO C++26. */

#include "anoptic_compose.h"

enum class Error {
    invalid,
};

auto parse(int value) -> ano::Result<int, Error>
{
    return value;
}

ANO_LET(parseValue, parse);
