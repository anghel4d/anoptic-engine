/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0
 * Anoptic targets ISO C++26. */

#include "anoptic_compose.h"

enum class Error {
    invalid,
};

auto parse(bool value) noexcept -> ano::Result<int, Error>
{
    return value ? 1 : 0;
}

auto consume(float value) noexcept -> ano::Result<int, Error>
{
    return static_cast<int>(value);
}

const auto path = ano::compose(parse).and_then(consume);
