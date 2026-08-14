/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0
 * Anoptic targets ISO C++26. */

#include "anoptic_results.h"

ANO_RESULT_TYPE(AnoFirstResult,
    ANO_FIRST_ACCEPTED = 0,
    ANO_FIRST_REJECTED);

ANO_RESULT_TYPE(AnoSecondResult,
    ANO_SECOND_ACCEPTED = 0,
    ANO_SECOND_REJECTED);

constexpr AnoFirstResult invalid =
    ANO_RESULT(AnoFirstResult, ANO_SECOND_REJECTED);
