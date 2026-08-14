/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0
 * Anoptic targets ISO C++26. */

#include "anoptic_results.h"

ANO_RESULT_TYPE(AnoCResult,
    ANO_C_ACCEPTED = 0,
    ANO_C_REJECTED);

int main(void)
{
    AnoCResult result = ANO_RESULT(AnoCResult, ANO_C_ACCEPTED);
    return result.code == ANO_C_ACCEPTED ? 0 : 1;
}
