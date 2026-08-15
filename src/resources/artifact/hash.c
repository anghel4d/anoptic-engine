/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0
 * Anoptic targets ISO C++26. */
/*  == Anoptic Game Engine v0.0000001 == */

#include <anoptic_resources_typed.h>

using namespace ano;

#if defined(__x86_64__) || defined(_M_X64)
#include <immintrin.h>
#endif

namespace ano::detail {

#if defined(__x86_64__) || defined(_M_X64)

__attribute__((target("sha,ssse3,sse4.1")))
static void sha256_transform_x86(uint32_t state[8], const uint8_t block[64])
{
    const __m128i shuffle = _mm_set_epi64x(
        INT64_C(0x0c0d0e0f08090a0b), INT64_C(0x0405060700010203));
    __m128i state0 = _mm_loadu_si128(
        reinterpret_cast<const __m128i *>(state));
    __m128i state1 = _mm_loadu_si128(
        reinterpret_cast<const __m128i *>(state + 4));
    __m128i temporary = _mm_shuffle_epi32(state0, 0xb1);
    state1 = _mm_shuffle_epi32(state1, 0x1b);
    state0 = _mm_alignr_epi8(temporary, state1, 8);
    state1 = _mm_blend_epi16(state1, temporary, 0xf0);
    const __m128i saved0 = state0;
    const __m128i saved1 = state1;

    __m128i schedule[4];
    for (uint32_t group = 0; group < 16; ++group) {
        __m128i message;
        if (group < 4) {
            message = _mm_shuffle_epi8(
                _mm_loadu_si128(reinterpret_cast<const __m128i *>(
                    block + group * 16)), shuffle);
        } else {
            const uint32_t current = group & 3u;
            const uint32_t next = (group + 1u) & 3u;
            const uint32_t after = (group + 2u) & 3u;
            const uint32_t prior = (group + 3u) & 3u;
            message = _mm_sha256msg1_epu32(
                schedule[current], schedule[next]);
            message = _mm_add_epi32(
                message, _mm_alignr_epi8(
                    schedule[prior], schedule[after], 4));
            message = _mm_sha256msg2_epu32(message, schedule[prior]);
        }
        schedule[group & 3u] = message;

        const __m128i constants = _mm_loadu_si128(
            reinterpret_cast<const __m128i *>(sha256Constants + group * 4));
        __m128i rounds = _mm_add_epi32(message, constants);
        state1 = _mm_sha256rnds2_epu32(state1, state0, rounds);
        rounds = _mm_shuffle_epi32(rounds, 0x0e);
        state0 = _mm_sha256rnds2_epu32(state0, state1, rounds);
    }

    state0 = _mm_add_epi32(state0, saved0);
    state1 = _mm_add_epi32(state1, saved1);
    temporary = _mm_shuffle_epi32(state0, 0x1b);
    state1 = _mm_shuffle_epi32(state1, 0xb1);
    state0 = _mm_blend_epi16(temporary, state1, 0xf0);
    state1 = _mm_alignr_epi8(state1, temporary, 8);
    _mm_storeu_si128(reinterpret_cast<__m128i *>(state), state0);
    _mm_storeu_si128(reinterpret_cast<__m128i *>(state + 4), state1);
}

#endif

void sha256_transform_runtime(uint32_t state[8], const uint8_t block[64])
{
#if defined(__x86_64__) || defined(_M_X64)
    if (__builtin_cpu_supports("sha")) {
        sha256_transform_x86(state, block);
        return;
    }
#endif
    Sha256 scalar;
    memcpy(scalar.state, state, sizeof(scalar.state));
    memcpy(scalar.block, block, sizeof(scalar.block));
    scalar.transform_scalar();
    memcpy(state, scalar.state, sizeof(scalar.state));
}

} // namespace ano::detail

const char *ano::resource_error_string(AnoResourceError error)
{
    static constexpr auto names = ano::reflect_enum_names<AnoResourceError>(
        "ANO_RESOURCE_", ano::EnumNameCase::lower);
    const auto *name = names.find(static_cast<size_t>(error));
    return name == nullptr ? "unknown_resource_error" : *name;
}

ano::ResourceResult<AnoContentId> ano::resource_content_id(
    AnoResourceBytes bytes)
{
    if (bytes.data == nullptr && bytes.size != 0)
        return ano::failure(ANO_RESOURCE_INVALID_ARGUMENT);
    if (bytes.size > UINT64_MAX / 8)
        return ano::failure(ANO_RESOURCE_OVERFLOW);
    return ano::detail::sha256(bytes.data, bytes.size);
}
