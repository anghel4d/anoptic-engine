/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Euclidean percussion + phrase-end fills. Groove (A2) pins phrase pattern draws.

#ifndef ANO_MUSIC_PERC_H
#define ANO_MUSIC_PERC_H

#include <anoptic_meta.h>

#include "music_gen.h"
#include "music_ir.h"

// Drum ids in DRUMS dict order; names/pitches reflected from the enum.
struct AnoDrumContract final
{
    uint8_t pitch;
};

typedef enum AnoDrum
{
    ANO_DRUM_KICK   [[=AnoDrumContract{36}]] = 0,
    ANO_DRUM_RIM    [[=AnoDrumContract{37}]],
    ANO_DRUM_SNARE  [[=AnoDrumContract{38}]],
    ANO_DRUM_CHAT   [[=AnoDrumContract{42}]],
    ANO_DRUM_OHAT   [[=AnoDrumContract{46}]],
    ANO_DRUM_CRASH  [[=AnoDrumContract{49}]],
    ANO_DRUM_LTOM   [[=AnoDrumContract{45}]],
    ANO_DRUM_MTOM   [[=AnoDrumContract{47}]],
    ANO_DRUM_HTOM   [[=AnoDrumContract{50}]],
    ANO_DRUM_SHAKER [[=AnoDrumContract{70}]],
    ANO_DRUM_COUNT,
} AnoDrum;

// kick/rim/snare/... straight from the identifiers.
inline constexpr auto ANO_DRUM_NAME_REGISTRY =
    ano::reflect_enum_names<AnoDrum>("ANO_DRUM_", ano::EnumNameCase::lower);

consteval auto ano_reflect_drum_pitches()
{
    auto contracts = ano::reflect_dense_enum_contracts<AnoDrum, AnoDrumContract>();
    ano::EnumRegistry<uint8_t, ANO_DRUM_COUNT> result{};
    for (size_t i = 0; i < ANO_DRUM_COUNT; ++i) {
        if (contracts.values[i].pitch == 0 || contracts.values[i].pitch > 127)
            __builtin_abort();
        result.values[i] = contracts.values[i].pitch;
    }
    return result;
}

inline constexpr auto ANO_DRUM_PITCH_REGISTRY = ano_reflect_drum_pitches();

// Exact "drum:%s" rendering of the reflected name.
struct AnoDrumRoleText final
{
    char text[16];
};

consteval auto ano_reflect_drum_roles()
{
    ano::EnumRegistry<AnoDrumRoleText, ANO_DRUM_COUNT> result{};
    for (size_t i = 0; i < ANO_DRUM_COUNT; ++i) {
        const char *name = ANO_DRUM_NAME_REGISTRY.values[i];
        if (*name == '\0')
            __builtin_abort();
        char *dst = result.values[i].text;
        size_t n = 0;
        for (const char *p = "drum:"; *p != '\0'; ++p)
            dst[n++] = *p;
        for (const char *p = name; *p != '\0'; ++p) {
            if (n + 1 >= sizeof result.values[i].text)
                __builtin_abort();
            dst[n++] = *p;
        }
        if (n >= sizeof ((AnoMusicEvent *)0)->role)
            __builtin_abort();
    }
    return result;
}

inline constexpr auto ANO_DRUM_ROLE_REGISTRY = ano_reflect_drum_roles();

// Plain unsigned strcmp order == Python lexical sort of the name strings.
consteval int ano_drum_name_cmp(const char *a, const char *b)
{
    size_t i = 0;
    while (a[i] != '\0' && a[i] == b[i])
        ++i;
    return (int)(unsigned char)a[i] - (int)(unsigned char)b[i];
}

// rank = index in byte-sorted name order; replaces the runtime strcmp tie-break.
consteval auto ano_reflect_drum_ranks()
{
    ano::EnumRegistry<uint8_t, ANO_DRUM_COUNT> result{};
    for (size_t i = 0; i < ANO_DRUM_COUNT; ++i) {
        uint8_t rank = 0;
        for (size_t j = 0; j < ANO_DRUM_COUNT; ++j) {
            int c = ano_drum_name_cmp(ANO_DRUM_NAME_REGISTRY.values[j],
                                      ANO_DRUM_NAME_REGISTRY.values[i]);
            if (j != i && c == 0)
                __builtin_abort(); // duplicate names would collapse the sort
            rank += c < 0;
        }
        result.values[i] = rank;
    }
    return result;
}

inline constexpr auto ANO_DRUM_SORT_RANK = ano_reflect_drum_ranks();

static_assert(decltype(ANO_DRUM_NAME_REGISTRY)::count == ANO_DRUM_COUNT);

typedef struct AnoPercConfig
{
    double  fillBaseProb;      // 0.25
    double  fillTensionWeight; // 0.55
    int     ghostVelocity;     // 52
    int     kickVel, snareVel, chatVel, ohatVel, crashVel; // 100 96 64 70 106
} AnoPercConfig;

AnoPercConfig ano_perc_config_default(void);

typedef struct AnoGroove
{
    uint8_t  ghosts[8];
    uint32_t ghostCount;
    uint32_t hatDrops; // slot bitmask
    bool     ohat;     // pre-downbeat hat opens this phrase
} AnoGroove;

AnoGroove ano_make_groove(AnoMusicRng *rng, AnoMeter meter, double density,
                          double roughness);

typedef struct AnoPercResult
{
    AnoMusicEvent events[48];
    uint32_t      eventCount;
    bool          fill; // fed back as had_fill next bar
} AnoPercResult;

// groove NULL = per-bar rolls. hyperFill (B3) scales secondary mid-phrase fill chance.
void ano_generate_perc(const AnoHarmonicContext *ctx, AnoMeter meter,
                       const AnoGenParams *params, AnoPhrasePos pos, bool hadFill,
                       const AnoPercConfig *cfg, AnoMusicRng *rng,
                       const AnoGroove *groove, double hyperFill,
                       AnoPercResult *out);

#endif // ANO_MUSIC_PERC_H
