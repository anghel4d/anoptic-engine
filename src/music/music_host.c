/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Public face over the conductor. AnoMusicConfig = authored content; generator tuning stays default.

#include <anoptic_memory.h>
#include <anoptic_music.h>

#include <mimalloc.h>

#include <math.h>
#include <string.h>

#include <anoptic_meta.h>
#include "music_conductor.h"
#include "music_roles.h"
#include "music_project.h"

// Cadence cycle capacity: policy_of indexes cadencePolicies[phrase % count].
#define CADENCE_CYCLE_MAX 8u
static_assert(sizeof ((AnoMusicConfig *)0)->cadencePolicies == CADENCE_CYCLE_MAX
              && sizeof ((AnoEngineConfig *)0)->cadencePolicies == CADENCE_CYCLE_MAX,
              "cadence cycle capacity must match both configs");

// Motif clamp bound == rhythm/contour extent == ANO_MOTIF_MAX.
static_assert(sizeof ((AnoMotif *)0)->rhythm / sizeof(AnoRhythmNote) == ANO_MOTIF_MAX
              && sizeof ((AnoMotif *)0)->contour / sizeof(int) == ANO_MOTIF_MAX,
              "motif clamp bound must equal the motif buffers' extent");
static_assert(ANO_MOTIF_MAX
                  <= sizeof ((AnoPeriodPlanner *)0)->openingMelody[0] / sizeof(AnoPlacedNote),
              "planner opening-melody row must hold a full motif");

// Public <-> engine projection contract: policy manifest + named ignores.
// music_project.h compiles expand / ano_music_config_default from this spec.
template<>
struct ano::music::ProjSpec<AnoMusicConfig, AnoEngineConfig> final {
    static constexpr ProjPolicy policies[] = {
        { "keyTonic", ProjRule::pitch_class },
        { "mode", ProjRule::mode_gate },
        { "valence", ProjRule::narrow },
        { "energy", ProjRule::narrow },
        { "tension", ProjRule::narrow },
        { "phraseBars", ProjRule::positive },
        { "cadencePolicies", ProjRule::cadence_cycle },
        { "cadencePolicyCount", ProjRule::count_clamp },
        { "params", ProjRule::params_block },
        { "motifLibrary", ProjRule::motif_library },
        { "motifLibraryCount", ProjRule::count_clamp },
        { "melody", ProjRule::melody_flags },
    };
    static constexpr std::array<std::string_view, 0> pubIgnore{};
    // generator tuning stays engine-default
    static constexpr std::string_view implIgnore[] = {
        "harmony", "voicing", "bass", "counter", "arp", "perc",
    };
};

// Both directions total: every field projected or explicitly ignored.
static_assert(ano::music::PROJ_DIAG<AnoMusicConfig, AnoEngineConfig>.ok,
              ano::music::PROJ_DIAG<AnoMusicConfig, AnoEngineConfig>);
static_assert(ano::music::PROJ_DIAG<AnoMusicalParams, AnoGenParams>.ok,
              ano::music::PROJ_DIAG<AnoMusicalParams, AnoGenParams>);

// Count-clamp annotations must clamp to the real capacities.
static_assert(ano::music::proj_policy<AnoProjCountClamp>(
                  ano::music::proj_member_named(^^AnoMusicConfig, "cadencePolicyCount")).max
                      == CADENCE_CYCLE_MAX
              && ano::music::proj_policy<AnoProjCountClamp>(
                  ano::music::proj_member_named(^^AnoMusicConfig, "motifLibraryCount")).max
                      == ANO_SIG_MAX,
              "count-clamp annotations must match cycle/library capacities");

AnoMusicConfig ano_music_config_default(void)
{
    AnoEngineConfig e = ano_engine_config_default();
    // memset: initializer leaves padding unspecified; snapshot = bytes.
    AnoMusicConfig c;
    memset(&c, 0, sizeof c);
    ano::music::project_to_public(c, e);
    return c;
}

// Public config -> conductor config. Generator tuning stays default.
// Clamps: mode/cadence -> sentinel, counts -> array, keyTonic -> pitch class, motif.n -> buffers.
static void expand(const AnoMusicConfig *c, AnoEngineConfig *e)
{
    *e = ano_engine_config_default();
    ano::music::project_from_public(*e, *c);
}

// One allocation; engine stays pointer-free (snapshot = bytes).
AnoMusicEngine *ano_music_create(const AnoMusicConfig *cfg, uint64_t seed)
{
    AnoMusicEngine *e = static_cast<AnoMusicEngine *>(mi_malloc(sizeof *e));
    if (!e)
        return NULL;
    AnoMusicConfig def;
    if (!cfg) {
        def = ano_music_config_default();
        cfg = &def;
    }
    AnoEngineConfig full;
    expand(cfg, &full);
    ano_engine_init(e, seed, &full);
    return e;
}

void ano_music_destroy(AnoMusicEngine *e)
{
    mi_free(e);
}

/* Control */

void ano_music_set_affect(AnoMusicEngine *e, float valence, float energy,
                          float tension, bool urgent)
{
    ano_engine_set_affect(e, (double)valence, (double)energy, (double)tension, urgent);
}

void ano_music_request_key(AnoMusicEngine *e, int tonicPc, bool urgent)
{
    ano_engine_request_key(e, tonicPc, urgent);
}

void ano_music_request_motif(AnoMusicEngine *e, const char *tag)
{
    ano_engine_request_motif(e, tag);
}

// Pinnable Tier-2 names. Unknown name refused.
typedef enum OverrideId
{
    OV_TEMPO_BPM, OV_VELOCITY_CENTER, OV_ARTICULATION, OV_NOTE_DENSITY, OV_ROUGHNESS,
    OV_ACCENT_DEPTH, OV_REGISTER_CENTER, OV_HARMONIC_RHYTHM, OV_CADENCE_POLICY, OV_MODE,
    OV_TEXTURE, OV_FILTER_CUTOFF, OV_REVERB_SEND, OV_DELAY_SEND, OV_DRIVE, OV_STEREO_WIDTH,
    OV_COUNT,
} OverrideId;

inline constexpr auto OVERRIDE_NAMES = ano::reflect_enum_names<OverrideId>(
    "OV_", ano::EnumNameCase::lower);

static int override_id(const char *param)
{
    return static_cast<int>(OVERRIDE_NAMES.find(param, -1));
}

// Install one pin. Out-of-contract cadence/mode: no pin.
static void override_apply(AnoOverrides *o, int id, bool set, double v)
{
    switch (id) {
    case OV_TEMPO_BPM:
        o->hasTempoBpm = set && isfinite(v) && v > 0.0;
        o->tempoBpm = o->hasTempoBpm ? v : 0.0;
        break;
    case OV_VELOCITY_CENTER: o->hasVelocityCenter = set; o->velocityCenter = v; break;
    case OV_ARTICULATION: o->hasArticulation = set;   o->articulation = v; break;
    case OV_NOTE_DENSITY: o->hasNoteDensity = set;    o->noteDensity = v; break;
    case OV_ROUGHNESS:    o->hasRoughness = set;      o->roughness = v; break;
    case OV_ACCENT_DEPTH: o->hasAccentDepth = set;    o->accentDepth = (int)v; break;
    case OV_REGISTER_CENTER:
        o->hasRegisterCenter = set && v >= 0.0 && v <= 127.0;
        o->registerCenter = o->hasRegisterCenter ? (int)v : 0;
        break;
    case OV_HARMONIC_RHYTHM:
        o->hasHarmonicRhythm = set;
        o->harmonicRhythm = v;
        break;
    case OV_CADENCE_POLICY:
        o->hasCadencePolicy = set && cadence_ok(v);
        o->cadencePolicy = o->hasCadencePolicy ? (int8_t)v : (int8_t)ANO_CADENCE_NONE;
        break;
    case OV_MODE:
        o->hasMode = set && mode_ok(v);
        o->mode = o->hasMode ? (int)v : ANO_MODE_NONE;
        break;
    case OV_TEXTURE: o->hasTexture = set;       o->texture = (AnoTexture)(int)v; break;
    case OV_FILTER_CUTOFF: o->hasFilterCutoff = set; o->filterCutoff = v; break;
    case OV_REVERB_SEND:   o->hasReverbSend = set;   o->reverbSend = v; break;
    case OV_DELAY_SEND:    o->hasDelaySend = set;    o->delaySend = v; break;
    case OV_DRIVE:         o->hasDrive = set;        o->drive = v; break;
    case OV_STEREO_WIDTH:  o->hasStereoWidth = set;  o->stereoWidth = v; break;
    default: break;
    }
}

bool ano_music_set_override(AnoMusicEngine *e, const char *param, double value)
{
    int id = override_id(param);
    if (id < 0)
        return false;
    override_apply(&e->overrides, id, true, value);
    return true;
}

void ano_music_clear_override(AnoMusicEngine *e, const char *param)
{
    int id = override_id(param);
    if (id >= 0)
        override_apply(&e->overrides, id, false, 0.0);
}

/* Generation */

void ano_music_advance_bar(AnoMusicEngine *e, AnoMusicBar *out)
{
    static thread_local AnoBarResult r; // 33 KB: too fat for the audio stack
    int keyBefore = e->scale.tonic;
    ano_engine_advance_bar(e, &r);

    out->eventCount = r.eventCount < ANO_MUSIC_MAX_BAR_EVENTS
                          ? r.eventCount
                          : ANO_MUSIC_MAX_BAR_EVENTS;
    for (uint32_t i = 0; i < out->eventCount; ++i)
        out->events[i] = r.events[i].core;
    out->params = ano_gen_params_bridge(&r.params);
    out->affect = ano_affect_bridge(r.affect);
    out->tempoCount = r.tempoPointCount < ANO_MUSIC_MAX_TEMPO ? r.tempoPointCount
                                                              : ANO_MUSIC_MAX_TEMPO;
    for (uint32_t i = 0; i < out->tempoCount; ++i)
        out->tempo[i] = (AnoTempoPoint){ r.tempoPoints[i].beat, r.tempoPoints[i].bpm };

    // what the bar MEANS: the payload gameplay reacts to
    AnoMusicMeaning *m = &out->meaning;
    m->bar = r.bar;
    m->keyTonic = e->scale.tonic;
    m->mode = e->scale.mode;
    m->chordDegree = r.context.chord.valid ? r.context.chord.degree : 0;
    m->chordInversion = r.context.chord.inversion;
    m->cadencePolicy = r.context.cadenceSlot == ANO_CTX_SLOT_NONE
                           ? ANO_CADENCE_NONE
                           : r.context.cadencePolicy;
    m->isCadence = r.context.cadenceSlot == ANO_CTX_SLOT_CADENCE;
    m->keyArrived = e->scale.tonic != keyBefore; // the modulation landed here
    m->motifStated = false;
    for (uint32_t i = 0; i < r.eventCount && !m->motifStated; ++i)
        m->motifStated = strcmp(r.events[i].role, ano_role_text(ANO_ROLE_MOTIF)) == 0;
}

double ano_music_bar_quarters(const AnoMusicEngine *e)
{
    return ano_meter_bar_quarters(e->config.meter);
}

int ano_music_next_bar(const AnoMusicEngine *e)
{
    return e->st.bar;
}

/* Snapshot / restore */

size_t ano_music_snapshot_size(void)
{
    return sizeof(AnoMusicEngine);
}

bool ano_music_snapshot(const AnoMusicEngine *e, void *buf, size_t cap)
{
    if (!e || !buf || cap < sizeof *e)
        return false;
    memcpy(buf, e, sizeof *e); // pointer-free by construction
    return true;
}

bool ano_music_restore(AnoMusicEngine *e, const void *buf, size_t len)
{
    if (!e || !buf || len != sizeof *e)
        return false;
    memcpy(e, buf, sizeof *e);
    return true;
}
