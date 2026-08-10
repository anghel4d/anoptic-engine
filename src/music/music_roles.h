/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Role vocabulary for AnoMusicEvent.role. The enum is the compile-time source of the
// texts and the lint licenses; the event keeps char storage 〜 tests author free text
// and the digest hashes the bytes.

#ifndef ANO_MUSIC_ROLES_H
#define ANO_MUSIC_ROLES_H

#include <string.h>

#include <anoptic_meta.h>

#include "music_ir.h"

// Exact digest text + music_verify licenses. chromatic implies non-chord.
struct AnoMusicRoleContract final
{
    char text[16];
    bool chromaticLicense;
    bool nonChordLicense;
};

typedef enum AnoMusicRole : uint8_t
{
    ANO_ROLE_NONE         [[=AnoMusicRoleContract{"", false, false}]] = 0,
    ANO_ROLE_CHORD_TONE   [[=AnoMusicRoleContract{"chord-tone", false, false}]],
    ANO_ROLE_BORROWED     [[=AnoMusicRoleContract{"borrowed", true, true}]],
    ANO_ROLE_CHROMATIC    [[=AnoMusicRoleContract{"chromatic", true, true}]], // whitelisted, never emitted
    ANO_ROLE_PASSING      [[=AnoMusicRoleContract{"passing", false, true}]],
    ANO_ROLE_NEIGHBOR     [[=AnoMusicRoleContract{"neighbor", false, true}]],
    ANO_ROLE_APPOGGIATURA [[=AnoMusicRoleContract{"appoggiatura", false, true}]],
    ANO_ROLE_SUSPENSION   [[=AnoMusicRoleContract{"suspension", false, true}]],
    ANO_ROLE_PEDAL        [[=AnoMusicRoleContract{"pedal", false, true}]],
    ANO_ROLE_APPROACH     [[=AnoMusicRoleContract{"approach", true, true}]],
    ANO_ROLE_ROOT         [[=AnoMusicRoleContract{"root", false, false}]],
    ANO_ROLE_MOTIF        [[=AnoMusicRoleContract{"motif", true, true}]],
    ANO_ROLE_PICKUP       [[=AnoMusicRoleContract{"pickup", false, false}]],
    ANO_ROLE_DOUBLING     [[=AnoMusicRoleContract{"doubling", true, true}]],
    ANO_ROLE_IMITATION    [[=AnoMusicRoleContract{"imitation", true, true}]],
    ANO_ROLE_ECHO         [[=AnoMusicRoleContract{"echo", true, true}]],
    ANO_ROLE_RESOLUTION   [[=AnoMusicRoleContract{"resolution", false, false}]], // pad ornament release
    ANO_ROLE_COUNT,
} AnoMusicRole;

inline constexpr auto ANO_ROLE_REGISTRY =
    ano::reflect_dense_enum_contracts<AnoMusicRole, AnoMusicRoleContract>();

// Texts unique, NUL-terminated, sized for the event field; "" only on the none role.
// License flags reproduce the linter whitelists: 7 chromatic, 12 non-chord, subset.
consteval bool ano_role_registry_valid()
{
    size_t chromatic = 0, nonchord = 0;
    for (size_t i = 0; i < ANO_ROLE_COUNT; ++i) {
        const AnoMusicRoleContract &c = ANO_ROLE_REGISTRY.values[i];
        size_t len = 0;
        while (len < sizeof c.text && c.text[len] != '\0')
            ++len;
        if (len >= sizeof c.text || len >= sizeof ((AnoMusicEvent *)0)->role)
            return false;
        if ((len == 0) != (i == ANO_ROLE_NONE))
            return false;
        if (c.chromaticLicense && !c.nonChordLicense)
            return false;
        chromatic += c.chromaticLicense;
        nonchord += c.nonChordLicense;
        for (size_t j = 0; j < i; ++j)
            if (ano::enum_name_equal(c.text, ANO_ROLE_REGISTRY.values[j].text))
                return false;
    }
    return chromatic == 7 && nonchord == 12;
}

static_assert(ano_role_registry_valid());

static inline constexpr const char *ano_role_text(AnoMusicRole r)
{
    return ANO_ROLE_REGISTRY.values[r < ANO_ROLE_COUNT ? r : ANO_ROLE_NONE].text;
}

static inline void ano_event_set_role(AnoMusicEvent *e, AnoMusicRole r)
{
    strncpy(e->role, ano_role_text(r), sizeof e->role - 1);
}

#endif // ANO_MUSIC_ROLES_H
