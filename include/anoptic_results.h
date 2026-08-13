/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0
 * Anoptic targets ISO C++26. */
/*  == Anoptic Game Engine v0.0000001 == */

/**
 * @file anoptic_results.h
 * @brief Typed, domain-local outcomes for fallible public operations.
 *
 * Not a scalar. Inspect .code. Domains own their codes; success is zero.
 */

#ifndef ANOPTIC_RESULTS_H
#define ANOPTIC_RESULTS_H

// Nodiscard wrapper around a domain-local code. Success is zero.
#define ANO_RESULT_TYPE(name, ...)                          \
    typedef enum name##Code { __VA_ARGS__ } name##Code;     \
    typedef struct [[nodiscard]] name {                     \
        name##Code code;                                    \
    } name

// Construct name from one of its domain-local codes.
#define ANO_RESULT(name, value) ((name){ .code = (value) })

#endif // ANOPTIC_RESULTS_H
