/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Bus insert-effect slots (private to src/audio/). Chain fixed at init.
// Process interleaved stereo in place. No alloc after init. Delay mem from module heap.

#ifndef ANO_AUDIO_FX_H
#define ANO_AUDIO_FX_H

#include <anoptic_audio.h>
#include <mimalloc.h>

#include "dsp/smooth.h"
#include "dsp/svf.h"
#include "dsp/biquad.h"
#include "dsp/delay.h"
#include "dsp/dynamics.h"

// FX_SET binding contract: param id + clamp range + write transform, consumed by the
// consteval registry in audio_fx.cpp. filter_mode ignores min/max (FilterMode validates).
enum class AnoAudioFxTransform : uint8_t { smooth_target, pole_ms, filter_mode };

struct AnoAudioFxBinding final
{
    uint32_t param; // AnoAudioFxParam
    float    min, max;
    AnoAudioFxTransform transform;
};

// Union link: ties each AnoAudioFx payload member to its owning AnoAudioEffectKind.
struct AnoAudioFxPayloadFor final
{
    AnoAudioEffectKind    kind;
    AnoAudioFxPayloadKind payload;
};

typedef struct AnoAudioFxFilter
{
    uint32_t mode [[=AnoAudioFxBinding{ANO_AUDIO_P_FILTER_MODE, 0.0f, 0.0f, AnoAudioFxTransform::filter_mode}]]; // AnoAudioFilterMode
    AnoAudioSmooth cutoff [[=AnoAudioFxBinding{ANO_AUDIO_P_FILTER_CUTOFF, 20.0f, 20000.0f, AnoAudioFxTransform::smooth_target}]],
                   q      [[=AnoAudioFxBinding{ANO_AUDIO_P_FILTER_Q, 0.1f, 12.0f, AnoAudioFxTransform::smooth_target}]];
    AnoDspSvfCoef  c;
    AnoDspSvfState s[2];
} AnoAudioFxFilter;

typedef struct AnoAudioFxEq3
{
    AnoAudioSmooth lowDb  [[=AnoAudioFxBinding{ANO_AUDIO_P_EQ_LOW_GAIN_DB, -24.0f, 24.0f, AnoAudioFxTransform::smooth_target}]],
                   lowF   [[=AnoAudioFxBinding{ANO_AUDIO_P_EQ_LOW_FREQ, 20.0f, 2000.0f, AnoAudioFxTransform::smooth_target}]],
                   midDb  [[=AnoAudioFxBinding{ANO_AUDIO_P_EQ_MID_GAIN_DB, -24.0f, 24.0f, AnoAudioFxTransform::smooth_target}]],
                   midF   [[=AnoAudioFxBinding{ANO_AUDIO_P_EQ_MID_FREQ, 100.0f, 10000.0f, AnoAudioFxTransform::smooth_target}]],
                   midQ   [[=AnoAudioFxBinding{ANO_AUDIO_P_EQ_MID_Q, 0.1f, 12.0f, AnoAudioFxTransform::smooth_target}]],
                   highDb [[=AnoAudioFxBinding{ANO_AUDIO_P_EQ_HIGH_GAIN_DB, -24.0f, 24.0f, AnoAudioFxTransform::smooth_target}]],
                   highF  [[=AnoAudioFxBinding{ANO_AUDIO_P_EQ_HIGH_FREQ, 1000.0f, 20000.0f, AnoAudioFxTransform::smooth_target}]];
    AnoDspBiquad      cl, cm, ch;
    AnoDspBiquadState sl[2], sm[2], sh[2];
} AnoAudioFxEq3;

typedef struct AnoAudioFxDc
{
    float R;             // pole (~5 Hz)
    float x1[2], y1[2];
} AnoAudioFxDc;

typedef struct AnoAudioFxDrive
{
    AnoAudioSmooth amount [[=AnoAudioFxBinding{ANO_AUDIO_P_DRIVE_AMOUNT, 0.1f, 16.0f, AnoAudioFxTransform::smooth_target}]],
                   trim   [[=AnoAudioFxBinding{ANO_AUDIO_P_DRIVE_TRIM, 0.0f, 4.0f, AnoAudioFxTransform::smooth_target}]];
} AnoAudioFxDrive;

typedef struct AnoAudioFxComp
{
    AnoAudioSmooth threshold [[=AnoAudioFxBinding{ANO_AUDIO_P_COMP_THRESHOLD, 0.01f, 1.0f, AnoAudioFxTransform::smooth_target}]],
                   ratio     [[=AnoAudioFxBinding{ANO_AUDIO_P_COMP_RATIO, 1.0f, 20.0f, AnoAudioFxTransform::smooth_target}]],
                   makeup    [[=AnoAudioFxBinding{ANO_AUDIO_P_COMP_MAKEUP, 0.25f, 4.0f, AnoAudioFxTransform::smooth_target}]];
    float attackCoef  [[=AnoAudioFxBinding{ANO_AUDIO_P_COMP_ATTACK_MS, 0.1f, 500.0f, AnoAudioFxTransform::pole_ms}]];
    float releaseCoef [[=AnoAudioFxBinding{ANO_AUDIO_P_COMP_RELEASE_MS, 1.0f, 2000.0f, AnoAudioFxTransform::pole_ms}]];
    float env;  // stereo-linked envelope
    float gain; // previous sample gain (feedback)
} AnoAudioFxComp;

typedef struct AnoAudioFxLim
{
    AnoAudioSmooth ceiling [[=AnoAudioFxBinding{ANO_AUDIO_P_LIM_CEILING, 0.1f, 1.0f, AnoAudioFxTransform::smooth_target}]];
    float          releaseCoef [[=AnoAudioFxBinding{ANO_AUDIO_P_LIM_RELEASE_MS, 1.0f, 1000.0f, AnoAudioFxTransform::pole_ms}]];
    float          gain;
    uint32_t       lookahead; // samples (5 ms at init)
    AnoDspDelay    dl[2];
    AnoDspWinMax   wm;
} AnoAudioFxLim;

typedef struct AnoAudioFxChorus
{
    AnoAudioSmooth rate  [[=AnoAudioFxBinding{ANO_AUDIO_P_CHORUS_RATE_HZ, 0.01f, 8.0f, AnoAudioFxTransform::smooth_target}]],
                   depth [[=AnoAudioFxBinding{ANO_AUDIO_P_CHORUS_DEPTH_MS, 0.1f, 12.0f, AnoAudioFxTransform::smooth_target}]],
                   mix   [[=AnoAudioFxBinding{ANO_AUDIO_P_CHORUS_MIX, 0.0f, 1.0f, AnoAudioFxTransform::smooth_target}]]; // Hz, ms, 0..1
    double         phase;            // LFO cycles [0, 1)
    AnoDspDelay    dl[2];
} AnoAudioFxChorus;

typedef struct AnoAudioFxReverb
{
    AnoAudioSmooth predelayMs [[=AnoAudioFxBinding{ANO_AUDIO_P_REV_PREDELAY_MS, 0.0f, 190.0f, AnoAudioFxTransform::smooth_target}]],
                   t60        [[=AnoAudioFxBinding{ANO_AUDIO_P_REV_T60_S, 0.1f, 12.0f, AnoAudioFxTransform::smooth_target}]],
                   dampHz     [[=AnoAudioFxBinding{ANO_AUDIO_P_REV_DAMP_HZ, 500.0f, 18000.0f, AnoAudioFxTransform::smooth_target}]],
                   mix        [[=AnoAudioFxBinding{ANO_AUDIO_P_REV_MIX, 0.0f, 1.0f, AnoAudioFxTransform::smooth_target}]];
    AnoDspDelay    pre;
    AnoDspAllpass  ap[2];
    AnoDspDelay    line[4];
    uint32_t       lineLen[4];
    float          lineSec[4];
    float          dampState[4];
    float          dampCoef;
    float          lineGain[4];
    AnoDspBiquad      shelfC;
    AnoDspBiquadState shelfS[2];
} AnoAudioFxReverb;

typedef struct AnoAudioFxPingpong
{
    AnoAudioSmooth timeMs   [[=AnoAudioFxBinding{ANO_AUDIO_P_PP_TIME_MS, 10.0f, 1100.0f, AnoAudioFxTransform::smooth_target}]],
                   feedback [[=AnoAudioFxBinding{ANO_AUDIO_P_PP_FEEDBACK, 0.0f, 0.95f, AnoAudioFxTransform::smooth_target}]],
                   mix      [[=AnoAudioFxBinding{ANO_AUDIO_P_PP_MIX, 0.0f, 1.0f, AnoAudioFxTransform::smooth_target}]];
    AnoDspDelay    dl[2];
} AnoAudioFxPingpong;

typedef struct AnoAudioFxWidth
{
    AnoAudioSmooth amount [[=AnoAudioFxBinding{ANO_AUDIO_P_WIDTH_AMOUNT, 0.0f, 2.0f, AnoAudioFxTransform::smooth_target}]]; // 0 mono .. 1 unity .. 2 wide
} AnoAudioFxWidth;

typedef struct AnoAudioFx
{
    uint32_t kind;   // AnoAudioEffectKind
    bool     bypass;
    float    fs;     // engine rate at init
    union {
        AnoAudioFxFilter   filter [[=AnoAudioFxPayloadFor{ANO_AUDIO_FX_FILTER, AnoAudioFxPayloadKind::filter}]];
        AnoAudioFxEq3      eq3 [[=AnoAudioFxPayloadFor{ANO_AUDIO_FX_EQ3, AnoAudioFxPayloadKind::eq3}]];
        AnoAudioFxDc       dc [[=AnoAudioFxPayloadFor{ANO_AUDIO_FX_DCBLOCK, AnoAudioFxPayloadKind::dc}]];
        AnoAudioFxDrive    drive [[=AnoAudioFxPayloadFor{ANO_AUDIO_FX_DRIVE, AnoAudioFxPayloadKind::drive}]];
        AnoAudioFxComp     comp [[=AnoAudioFxPayloadFor{ANO_AUDIO_FX_COMPRESSOR, AnoAudioFxPayloadKind::comp}]];
        AnoAudioFxLim      lim [[=AnoAudioFxPayloadFor{ANO_AUDIO_FX_LIMITER, AnoAudioFxPayloadKind::lim}]];
        AnoAudioFxChorus   chorus [[=AnoAudioFxPayloadFor{ANO_AUDIO_FX_CHORUS, AnoAudioFxPayloadKind::chorus}]];
        AnoAudioFxReverb   reverb [[=AnoAudioFxPayloadFor{ANO_AUDIO_FX_REVERB, AnoAudioFxPayloadKind::reverb}]];
        AnoAudioFxPingpong pp [[=AnoAudioFxPayloadFor{ANO_AUDIO_FX_PINGPONG, AnoAudioFxPayloadKind::pingpong}]];
        AnoAudioFxWidth    width [[=AnoAudioFxPayloadFor{ANO_AUDIO_FX_WIDTH, AnoAudioFxPayloadKind::width}]];
    } u;
} AnoAudioFx;

#ifdef __cplusplus
extern "C" {
#endif

// NONE = empty pass-through. false on alloc failure.
bool ano_audio_fx_init(AnoAudioFx *fx, uint32_t kind, mi_heap_t *heap,
                       uint32_t sampleRate, float coefBlock);

// Retarget one param at block boundary. Unknown/OOR clamped or dropped (debug warn).
void ano_audio_fx_set(AnoAudioFx *fx, uint32_t paramId, float value);

// Interleaved stereo in place. NONE/bypass = no-op.
void ano_audio_fx_process(AnoAudioFx *fx, float *stereo, uint32_t frames, uint32_t sampleRate);

#ifdef __cplusplus
}
#endif

#endif // ANO_AUDIO_FX_H
