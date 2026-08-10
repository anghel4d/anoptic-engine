/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Consteval projection compiler: public config/params <-> engine twins.
// AnoProj* annotations (anoptic_music.h) name every exceptional field rule; ProjSpec<Pub, Impl>
// pins the manifest plus explicit ignore lists; every other field must be an identical-type
// same-identifier twin. proj_check proves both directions total before any body is generated.
// Invariant: destinations are zero/default-initialized before projection 〜 generated code
// assigns named members only, so padding stays zeroed (snapshot = bytes).

#ifndef ANO_MUSIC_PROJECT_H
#define ANO_MUSIC_PROJECT_H

#include <anoptic_music.h>

#include <anoptic_meta.h>

#include <array>

#include "music_ir.h"

// mode/cadence table-index ingress. In: any double. Out: true only in contract. NaN fails both.
inline bool mode_ok(double v)
{
    return v >= static_cast<double>(ANO_MODE_NONE)
        && v < static_cast<double>(ANO_MODE_COUNT);
}

inline bool cadence_ok(double v)
{
    return v >= static_cast<double>(ANO_CADENCE_AUTHENTIC)
        && v < static_cast<double>(ANO_CADENCE_COUNT);
}

namespace ano::music {

// Field rules, one per AnoProj* policy; none = identical-type twin copy.
enum class ProjRule : uint8_t {
    none = 0,
    narrow,
    pitch_class,
    mode_gate,
    cadence_cycle,
    count_clamp,
    positive,
    clamp,
    layer_mask,
    elem_cast,
    motif_library,
    params_block,
    melody_flags,
};

struct ProjPolicy final {
    std::string_view field;
    ProjRule rule;
};

// One specialization per projected pair: policy manifest + explicit ignore lists.
template<class Pub, class Impl>
struct ProjSpec;

consteval auto proj_members(std::meta::info type)
{
    return std::meta::nonstatic_data_members_of(type, std::meta::access_context::current());
}

consteval std::meta::info proj_member_named(std::meta::info type, std::string_view name)
{
    for (auto member : proj_members(type))
        if (std::meta::identifier_of(member) == name)
            return member;
    return {};
}

consteval std::meta::info proj_twin(std::meta::info implType, std::meta::info pubMember)
{
    return proj_member_named(implType, std::meta::identifier_of(pubMember));
}

template<class Policy>
consteval bool proj_has(std::meta::info member)
{
    return std::meta::annotations_of_with_type(member, ^^Policy).size() == 1;
}

template<class Policy>
consteval Policy proj_policy(std::meta::info member)
{
    return std::meta::extract<Policy>(
        std::meta::annotations_of_with_type(member, ^^Policy)[0]);
}

consteval ProjRule proj_rule(std::meta::info member, size_t& hits)
{
    ProjRule rule = ProjRule::none;
    hits = 0;
    const auto tally = [&](bool has, ProjRule r) {
        if (has) {
            rule = r;
            ++hits;
        }
    };
    tally(proj_has<AnoProjNarrow>(member), ProjRule::narrow);
    tally(proj_has<AnoProjPitchClass>(member), ProjRule::pitch_class);
    tally(proj_has<AnoProjModeGate>(member), ProjRule::mode_gate);
    tally(proj_has<AnoProjCadenceCycle>(member), ProjRule::cadence_cycle);
    tally(proj_has<AnoProjCountClamp>(member), ProjRule::count_clamp);
    tally(proj_has<AnoProjPositive>(member), ProjRule::positive);
    tally(proj_has<AnoProjClamp>(member), ProjRule::clamp);
    tally(proj_has<AnoProjLayerMask>(member), ProjRule::layer_mask);
    tally(proj_has<AnoProjElemCast>(member), ProjRule::elem_cast);
    tally(proj_has<AnoProjMotifLibrary>(member), ProjRule::motif_library);
    tally(proj_has<AnoProjParamsBlock>(member), ProjRule::params_block);
    tally(proj_has<AnoProjMelodyFlags>(member), ProjRule::melody_flags);
    return rule;
}

consteval ProjRule proj_rule(std::meta::info member)
{
    size_t hits = 0;
    return proj_rule(member, hits);
}

consteval size_t proj_extent(std::meta::info arrayType)
{
    return std::meta::size_of(arrayType)
         / std::meta::size_of(std::meta::remove_extent(arrayType));
}

template<class Pub, class Impl>
consteval bool proj_pub_ignored(std::string_view name)
{
    for (std::string_view entry : ProjSpec<Pub, Impl>::pubIgnore)
        if (entry == name)
            return true;
    return false;
}

// static_assert payload: the first violation names the broken rule and the field.
struct ProjDiag final {
    bool ok = true;
    size_t length = 0;
    char text[192] = {};

    constexpr const char* data() const { return text; }
    constexpr size_t size() const { return length; }

    constexpr void fail(std::string_view what, std::string_view field)
    {
        if (!ok)
            return; // first failure wins
        ok = false;
        for (char c : what)
            if (length < sizeof text)
                text[length++] = c;
        for (char c : field)
            if (length < sizeof text)
                text[length++] = c;
    }
};

// Totality proof for one pair, both directions.
// Public side: annotations must equal the manifest; policy'd fields must be structurally sound;
// the rest need an identical-type twin or a pubIgnore entry. Engine side: every field is a
// projected twin, a policy peer (layers/layerCount), or an implIgnore entry.
// Ignore lists must name real, otherwise-uncovered fields.
template<class Pub, class Impl>
consteval ProjDiag proj_check()
{
    ProjDiag d{};
    using Spec = ProjSpec<Pub, Impl>;
    constexpr auto absent = std::meta::info{};

    const auto listed = [](const auto& list, std::string_view name) {
        for (std::string_view entry : list)
            if (entry == name)
                return true;
        return false;
    };
    const auto manifest = [](std::string_view name) {
        for (const ProjPolicy& entry : Spec::policies)
            if (entry.field == name)
                return entry.rule;
        return ProjRule::none;
    };

    for (size_t i = 0; i < std::size(Spec::policies); ++i) {
        if (proj_member_named(^^Pub, Spec::policies[i].field) == absent)
            d.fail("manifest names unknown public field: ", Spec::policies[i].field);
        for (size_t j = i + 1; j < std::size(Spec::policies); ++j)
            if (Spec::policies[j].field == Spec::policies[i].field)
                d.fail("manifest lists field twice: ", Spec::policies[i].field);
    }

    for (auto pm : proj_members(^^Pub)) {
        const std::string_view name = std::meta::identifier_of(pm);
        size_t hits = 0;
        const ProjRule rule = proj_rule(pm, hits);
        if (hits > 1)
            d.fail("multiple projection policies on: ", name);
        if (listed(Spec::pubIgnore, name)) {
            if (rule != ProjRule::none)
                d.fail("ignored public field carries a policy: ", name);
            continue;
        }
        if (rule != manifest(name)) {
            d.fail("policy annotation does not match the manifest: ", name);
            continue;
        }
        const auto tm = proj_twin(^^Impl, pm);
        const auto pubType = std::meta::dealias(std::meta::type_of(pm));
        const auto implType =
            tm == absent ? absent : std::meta::dealias(std::meta::type_of(tm));
        if (rule != ProjRule::layer_mask && tm == absent) {
            d.fail("unprojected public field (no engine twin, no policy): ", name);
            continue;
        }
        switch (rule) {
        case ProjRule::none:
            if (implType != pubType)
                d.fail("same-name field with incompatible type needs a policy: ", name);
            break;
        case ProjRule::narrow: {
            const bool pubIsFloat = pubType == ^^float;
            const bool pubIsDouble = pubType == ^^double;
            const bool implIsFloat = implType == ^^float;
            const bool implIsDouble = implType == ^^double;
            if (!((pubIsFloat && implIsDouble) || (pubIsDouble && implIsFloat)))
                d.fail("narrow policy needs a float/double twin: ", name);
            break;
        }
        case ProjRule::pitch_class:
        case ProjRule::mode_gate:
        case ProjRule::positive:
        case ProjRule::count_clamp:
            if (implType != pubType || !std::meta::is_integral_type(pubType))
                d.fail("integral policy needs an identical integral twin: ", name);
            break;
        case ProjRule::clamp:
            if (!std::meta::is_integral_type(pubType)
                || !std::meta::is_integral_type(implType))
                d.fail("clamp policy needs integral twins: ", name);
            break;
        case ProjRule::cadence_cycle:
            if (implType != pubType || !std::meta::is_array_type(pubType))
                d.fail("array policy needs an identical array twin: ", name);
            break;
        case ProjRule::elem_cast:
            if (!std::meta::is_array_type(pubType) || !std::meta::is_array_type(implType)
                || proj_extent(pubType) != proj_extent(implType)
                || !std::meta::is_integral_type(std::meta::remove_extent(pubType))
                || !std::meta::is_integral_type(std::meta::remove_extent(implType)))
                d.fail("elem_cast policy needs equal-extent integral arrays: ", name);
            break;
        case ProjRule::layer_mask: {
            const auto layers = proj_member_named(^^Impl, "layers");
            const auto layerCount = proj_member_named(^^Impl, "layerCount");
            if (tm != absent)
                d.fail("layer_mask field must not have a same-name twin: ", name);
            else if (layers == absent || layerCount == absent
                     || proj_extent(std::meta::type_of(layers)) != ANO_MUSIC_LAYER_COUNT)
                d.fail("layer_mask peers layers/layerCount missing or missized: ", name);
            break;
        }
        case ProjRule::motif_library:
            if (implType != pubType || !std::meta::is_array_type(pubType))
                d.fail("array policy needs an identical array twin: ", name);
            else if (proj_member_named(^^Pub, "motifLibraryCount") == absent)
                d.fail("motif_library needs motifLibraryCount beside: ", name);
            break;
        case ProjRule::params_block:
            if (proj_member_named(^^Pub, "hasMapper") == absent)
                d.fail("params_block needs hasMapper beside: ", name);
            break;
        case ProjRule::melody_flags:
            if (proj_member_named(implType, "planApex") == absent
                || proj_member_named(implType, "counterpoint") == absent
                || proj_member_named(pubType, "planApex") == absent
                || proj_member_named(pubType, "counterpoint") == absent)
                d.fail("melody_flags needs planApex/counterpoint on both twins: ", name);
            break;
        }
    }

    bool hasLayerMask = false;
    for (auto pm : proj_members(^^Pub))
        if (proj_rule(pm) == ProjRule::layer_mask)
            hasLayerMask = true;
    for (auto im : proj_members(^^Impl)) {
        const std::string_view name = std::meta::identifier_of(im);
        const bool projected = proj_member_named(^^Pub, name) != absent
                            && !listed(Spec::pubIgnore, name);
        const bool peer = hasLayerMask && (name == "layers" || name == "layerCount");
        if (listed(Spec::implIgnore, name)) {
            if (projected || peer)
                d.fail("ignored engine field is also projected: ", name);
            continue;
        }
        if (!projected && !peer)
            d.fail("engine field neither projected nor ignored: ", name);
    }
    for (std::string_view name : Spec::pubIgnore)
        if (proj_member_named(^^Pub, name) == absent)
            d.fail("public ignore names unknown field: ", name);
    for (std::string_view name : Spec::implIgnore)
        if (proj_member_named(^^Impl, name) == absent)
            d.fail("engine ignore names unknown field: ", name);
    return d;
}

template<class Pub, class Impl>
inline constexpr ProjDiag PROJ_DIAG = proj_check<Pub, Impl>();

// Public -> engine ingress. In: annotated Pub. Out: Impl already holding engine defaults.
// Generated per rule: named-member assignments only; unwritten fields keep their defaults.
template<class Pub, class Impl>
inline void project_from_public(Impl& dst, const Pub& src)
{
    static_assert(PROJ_DIAG<Pub, Impl>.ok, PROJ_DIAG<Pub, Impl>);
    static constexpr auto members = std::define_static_array(
        std::meta::nonstatic_data_members_of(^^Pub, std::meta::access_context::current()));
    template for (constexpr auto pm : members) {
        constexpr ProjRule rule = proj_rule(pm);
        constexpr auto tm = proj_twin(^^Impl, pm);
        if constexpr (proj_pub_ignored<Pub, Impl>(std::meta::identifier_of(pm))) {
        } else if constexpr (rule == ProjRule::none) {
            dst.[:tm:] = src.[:pm:];
        } else if constexpr (rule == ProjRule::narrow) {
            using To = [:std::meta::type_of(tm):];
            dst.[:tm:] = static_cast<To>(src.[:pm:]);
        } else if constexpr (rule == ProjRule::pitch_class) {
            // pitch class 0..11 (anoptic_music.h): normalize like mode/cadence below.
            // Stays signed: wander_target computes keyTonic + 7*step, step in {-1,+1}.
            dst.[:tm:] = ((src.[:pm:] % 12) + 12) % 12;
        } else if constexpr (rule == ProjRule::mode_gate) {
            dst.[:tm:] = mode_ok(src.[:pm:]) ? src.[:pm:] : ANO_MODE_NONE;
        } else if constexpr (rule == ProjRule::cadence_cycle) {
            constexpr size_t n = proj_extent(std::meta::type_of(pm));
            for (uint32_t i = 0; i < n; ++i)
                dst.[:tm:][i] = cadence_ok(src.[:pm:][i])
                                    ? src.[:pm:][i]
                                    : (int8_t)ANO_CADENCE_AUTHENTIC;
        } else if constexpr (rule == ProjRule::count_clamp) {
            constexpr auto policy = proj_policy<AnoProjCountClamp>(pm);
            dst.[:tm:] = src.[:pm:] < policy.max ? src.[:pm:] : policy.max;
        } else if constexpr (rule == ProjRule::positive) {
            if (src.[:pm:] > 0)
                dst.[:tm:] = src.[:pm:];
        } else if constexpr (rule == ProjRule::clamp) {
            dst.[:tm:] = src.[:pm:]; // clamp applies on the narrowing (to-public) side only
        } else if constexpr (rule == ProjRule::layer_mask) {
            constexpr auto layers = proj_member_named(^^Impl, "layers");
            constexpr auto layerCount = proj_member_named(^^Impl, "layerCount");
            dst.[:layerCount:] = 0;
            for (uint32_t l = 0; l < ANO_MUSIC_LAYER_COUNT; ++l)
                if (src.[:pm:] & (1u << l))
                    dst.[:layers:][dst.[:layerCount:]++] = (uint8_t)l;
        } else if constexpr (rule == ProjRule::elem_cast) {
            using Elem = [:std::meta::remove_extent(std::meta::type_of(tm)):];
            constexpr size_t n = proj_extent(std::meta::type_of(pm));
            for (uint32_t i = 0; i < n; ++i)
                dst.[:tm:][i] = static_cast<Elem>(src.[:pm:][i]);
        } else if constexpr (rule == ProjRule::motif_library) {
            constexpr auto count = proj_member_named(^^Pub, "motifLibraryCount");
            constexpr size_t cap = proj_extent(std::meta::type_of(tm));
            for (uint32_t i = 0; i < src.[:count:] && i < cap; ++i) {
                dst.[:tm:][i] = src.[:pm:][i];
                if (dst.[:tm:][i].motif.n > ANO_MOTIF_MAX) // authored count can't exceed the buffers
                    dst.[:tm:][i].motif.n = ANO_MOTIF_MAX;
            }
        } else if constexpr (rule == ProjRule::params_block) {
            // Bridge params -> ordered layer list from bitmask.
            if (!src.hasMapper)
                project_from_public(dst.[:tm:], src.[:pm:]);
        } else if constexpr (rule == ProjRule::melody_flags) {
            dst.[:tm:].planApex = src.[:pm:].planApex;
            dst.[:tm:].counterpoint = src.[:pm:].counterpoint;
        }
    }
}

// Engine -> public egress. In: engine Impl. Out: Pub previously zero-initialized
// (value-init/memset 〜 padding stays zeroed; layer_mask |= and motif_library rely on zero).
// Ingress-only gates (pitch_class/mode_gate/positive/count_clamp) copy raw here.
template<class Pub, class Impl>
inline void project_to_public(Pub& dst, const Impl& src)
{
    static_assert(PROJ_DIAG<Pub, Impl>.ok, PROJ_DIAG<Pub, Impl>);
    static constexpr auto members = std::define_static_array(
        std::meta::nonstatic_data_members_of(^^Pub, std::meta::access_context::current()));
    template for (constexpr auto pm : members) {
        constexpr ProjRule rule = proj_rule(pm);
        constexpr auto tm = proj_twin(^^Impl, pm);
        if constexpr (proj_pub_ignored<Pub, Impl>(std::meta::identifier_of(pm))) {
        } else if constexpr (rule == ProjRule::none) {
            dst.[:pm:] = src.[:tm:];
        } else if constexpr (rule == ProjRule::narrow) {
            using To = [:std::meta::type_of(pm):];
            dst.[:pm:] = static_cast<To>(src.[:tm:]);
        } else if constexpr (rule == ProjRule::pitch_class || rule == ProjRule::mode_gate
                             || rule == ProjRule::positive || rule == ProjRule::count_clamp) {
            dst.[:pm:] = src.[:tm:];
        } else if constexpr (rule == ProjRule::cadence_cycle) {
            constexpr size_t n = proj_extent(std::meta::type_of(pm));
            for (uint32_t i = 0; i < n; ++i)
                dst.[:pm:][i] = src.[:tm:][i];
        } else if constexpr (rule == ProjRule::clamp) {
            constexpr auto policy = proj_policy<AnoProjClamp>(pm);
            using To = [:std::meta::type_of(pm):];
            dst.[:pm:] = static_cast<To>(src.[:tm:] < policy.lo   ? policy.lo
                                         : src.[:tm:] > policy.hi ? policy.hi
                                                                  : src.[:tm:]);
        } else if constexpr (rule == ProjRule::layer_mask) {
            constexpr auto layers = proj_member_named(^^Impl, "layers");
            constexpr auto layerCount = proj_member_named(^^Impl, "layerCount");
            for (uint32_t i = 0; i < src.[:layerCount:]; ++i)
                if (src.[:layers:][i] < ANO_MUSIC_LAYER_COUNT)
                    dst.[:pm:] |= (uint8_t)(1u << src.[:layers:][i]);
        } else if constexpr (rule == ProjRule::elem_cast) {
            using Elem = [:std::meta::remove_extent(std::meta::type_of(pm)):];
            constexpr size_t n = proj_extent(std::meta::type_of(pm));
            for (uint32_t i = 0; i < n; ++i)
                dst.[:pm:][i] = static_cast<Elem>(src.[:tm:][i]);
        } else if constexpr (rule == ProjRule::motif_library) {
            // not copied back 〜 stays zero-init (the engine default library is empty)
        } else if constexpr (rule == ProjRule::params_block) {
            dst.[:pm:] = ano_gen_params_bridge(&src.[:tm:]);
        } else if constexpr (rule == ProjRule::melody_flags) {
            dst.[:pm:].planApex = src.[:tm:].planApex;
            dst.[:pm:].counterpoint = src.[:tm:].counterpoint;
        }
    }
}

// Params pair: public AnoMusicalParams <-> engine AnoGenParams
// (ano_gen_params_bridge egress; expand's static-params ingress).
template<>
struct ProjSpec<AnoMusicalParams, AnoGenParams> final {
    static constexpr ProjPolicy policies[] = {
        { "noteDensity", ProjRule::narrow },
        { "roughness", ProjRule::narrow },
        { "articulation", ProjRule::narrow },
        { "velocityCenter", ProjRule::clamp },
        { "accentDepth", ProjRule::clamp },
        { "registerCenter", ProjRule::clamp },
        { "layersActive", ProjRule::layer_mask },
        { "harmonicRhythm", ProjRule::narrow },
        { "dissonanceBudget", ProjRule::narrow },
        { "instruments", ProjRule::elem_cast },
        { "filterCutoff", ProjRule::narrow },
        { "reverbSend", ProjRule::narrow },
        { "delaySend", ProjRule::narrow },
        { "drive", ProjRule::narrow },
        { "stereoWidth", ProjRule::narrow },
    };
    static constexpr std::array<std::string_view, 0> pubIgnore{};
    // gen-only knobs: conductor-owned, no public counterpart
    static constexpr std::string_view implIgnore[] = { "cadencePolicy", "texture" };
};

} // namespace ano::music

#endif // ANO_MUSIC_PROJECT_H
