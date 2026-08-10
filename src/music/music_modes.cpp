/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Load-bearing mode metadata. Reflection projects each enum annotation exactly once.

#include "music_modes.h"

#include <anoptic_meta.h>

namespace {

using Mode = ano::EnumValue<AnoMode>;

inline constexpr auto kModeNames = ano::reflect_enum_names<AnoMode>(
    "ANO_MODE_", ano::EnumNameCase::lower);
inline constexpr auto kModeContracts =
    ano::reflect_dense_enum_contracts<AnoMode, AnoModeContract>();

struct ModeLookup final {
    uint16_t mask;       // bit i set iff some interval == i (tonic-relative pc offset)
    uint8_t degrees[12]; // pc offset from tonic -> degree 1..7, 0 = not in scale
};

// Derived consteval from kModeContracts.values[i].intervals 〜 the single source of truth.
inline constexpr auto kModeLookups = [] consteval {
    ano::EnumRegistry<ModeLookup, kModeContracts.count> out = {};
    for (std::size_t i = 0; i < kModeContracts.count; ++i) {
        const AnoModeContract &contract = kModeContracts.values[i];
        for (std::size_t degree = 0; degree < 7; ++degree) {
            const uint8_t offset = contract.intervals[degree];
            out.values[i].mask |= static_cast<uint16_t>(1u << offset);
            out.values[i].degrees[offset] = static_cast<uint8_t>(degree + 1);
        }
    }
    return out;
}();

consteval bool mode_table_valid()
{
    for (std::size_t i = 0; i < kModeNames.size(); ++i) {
        const AnoModeContract &contract = kModeContracts.values[i];
        if (kModeNames.values[i] == nullptr || kModeNames.values[i][0] == '\0'
            || contract.intervals[0] != 0)
            return false;
        for (std::size_t degree = 1; degree < 7; ++degree)
            if (contract.intervals[degree] <= contract.intervals[degree - 1]
                || contract.intervals[degree] >= 12)
                return false;
        const ModeLookup &lookup = kModeLookups.values[i];
        if (!(lookup.mask & 1u))
            return false;
        int bits = 0;
        for (int bit = 0; bit < 12; ++bit)
            bits += (lookup.mask >> bit) & 1;
        if (bits != 7)
            return false;
        for (std::size_t offset = 0; offset < 12; ++offset) {
            const uint8_t degree = lookup.degrees[offset];
            if (degree > 7 || (((lookup.mask >> offset) & 1) != 0) != (degree != 0))
                return false;
            if (degree != 0 && contract.intervals[degree - 1] != offset)
                return false;
        }
    }
    return true;
}

// Reference mirrors ano_scale_pcs + linear scan; proves the O(1) lookups bit-identical
// for every mode, tonic 0..11, and pc 0..11.
consteval bool mode_lookup_matches_reference()
{
    for (std::size_t i = 0; i < kModeContracts.count; ++i) {
        const AnoModeContract &contract = kModeContracts.values[i];
        const ModeLookup &lookup = kModeLookups.values[i];
        for (int tonic = 0; tonic < 12; ++tonic)
            for (int pc = 0; pc < 12; ++pc) {
                int refDegree = 0;
                for (int d = 0; d < 7 && refDegree == 0; ++d)
                    if ((tonic + contract.intervals[d]) % 12 == pc)
                        refDegree = d + 1;
                const int offset = (pc - tonic + 12) % 12;
                if (lookup.degrees[offset] != refDegree
                    || (((lookup.mask >> offset) & 1) != 0) != (refDegree != 0))
                    return false;
            }
    }
    return true;
}

constexpr Mode mode_or_ionian(AnoMode raw)
{
    return Mode::from(raw).value_or(Mode::constant<ANO_MODE_IONIAN>());
}

static_assert(mode_table_valid());
static_assert(mode_lookup_matches_reference());
static_assert(ano::Data<Mode>);
static_assert(ano::Data<AnoModeContract>);
static_assert(ano::Data<decltype(kModeContracts)>);
static_assert(ano::Data<ModeLookup>);
static_assert(ano::Data<decltype(kModeLookups)>);
static_assert(ano::Data<decltype(kModeNames)>);

} // namespace

const char *ano_mode_name(AnoMode mode)
{
    return kModeNames.values[mode_or_ionian(mode).index()];
}

int ano_mode_brightness(AnoMode mode)
{
    const auto parsed = Mode::from(mode);
    return parsed ? kModeContracts.values[parsed->index()].brightness : -1;
}

const uint8_t *ano_mode_intervals(AnoMode mode)
{
    return kModeContracts.values[mode_or_ionian(mode).index()].intervals;
}

uint16_t ano_mode_pc_mask(AnoMode mode)
{
    return kModeLookups.values[mode_or_ionian(mode).index()].mask;
}

const uint8_t *ano_mode_degrees(AnoMode mode)
{
    return kModeLookups.values[mode_or_ionian(mode).index()].degrees;
}
