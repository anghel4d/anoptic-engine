/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */

#include <meta>
#include <stdint.h>
#include <string_view>
#include <type_traits>

#include "vulkan_backend/vulkanMaster.h"
#include "vulkan_backend/text_raster.h"

namespace ano::gpu_abi {

enum class AnoGpuScalar : uint8_t
{
    invalid,
    f32,
    i32,
    u32,
};

enum class AnoGpuLayout : uint8_t
{
    std140,
    std430,
};

struct AnoGpuType final
{
    AnoGpuScalar scalar;
    uint32_t components;
    uint32_t alignment;
    uint32_t size;
};

struct AnoGpuField final
{
    std::string_view name;
    AnoGpuScalar scalar;
    uint32_t components;
    uint32_t offset;
    uint32_t size;
};

struct AnoGpuStruct final
{
    AnoGpuField fields[80];
    uint32_t count;
    uint32_t alignment;
    uint32_t size;
    bool valid;
};

struct AnoGpuCursor final
{
    std::string_view source;
    size_t at;

    consteval void skip()
    {
        for (;;) {
            while (at < source.size() && (source[at] == ' ' || source[at] == '\t' ||
                                          source[at] == '\r' || source[at] == '\n'))
                ++at;
            if (at + 1 >= source.size() || source[at] != '/')
                return;
            if (source[at + 1] == '/') {
                at += 2;
                while (at < source.size() && source[at] != '\n')
                    ++at;
            } else if (source[at + 1] == '*') {
                at += 2;
                while (at + 1 < source.size() && (source[at] != '*' || source[at + 1] != '/'))
                    ++at;
                if (at + 1 < source.size())
                    at += 2;
            } else {
                return;
            }
        }
    }

    consteval std::string_view identifier()
    {
        skip();
        const size_t begin = at;
        if (at >= source.size() ||
            !((source[at] >= 'a' && source[at] <= 'z') ||
              (source[at] >= 'A' && source[at] <= 'Z') || source[at] == '_'))
            return {};
        do {
            ++at;
        } while (at < source.size() &&
                 ((source[at] >= 'a' && source[at] <= 'z') ||
                  (source[at] >= 'A' && source[at] <= 'Z') ||
                  (source[at] >= '0' && source[at] <= '9') || source[at] == '_'));
        return source.substr(begin, at - begin);
    }

    consteval bool take(char token)
    {
        skip();
        if (at >= source.size() || source[at] != token)
            return false;
        ++at;
        return true;
    }

    consteval uint32_t integer()
    {
        skip();
        uint32_t value = 0;
        const size_t begin = at;
        while (at < source.size() && source[at] >= '0' && source[at] <= '9') {
            value = value * 10u + static_cast<uint32_t>(source[at] - '0');
            ++at;
        }
        return at == begin ? 0u : value;
    }
};

static constexpr char ano_gpu_abi_glsl[] = {
#embed "../../resources/shaders/gpu_abi.glsl"
, 0
};

static constexpr char ano_text_abi_glsl[] = {
#embed "../../resources/shaders/textcoverage.glsl"
, 0
};

static constexpr char ano_ui_abi_glsl[] = {
#embed "../../resources/shaders/uicoverage.glsl"
, 0
};

// UTF-8 shader comments carry bytes >= 0x80 〜 widen through unsigned char to dodge -Wnarrowing.
template<size_t N>
struct AnoGlslText final {
    char data[N];
};

template<size_t N>
consteval AnoGlslText<N> ano_gpu_glsl_text(const unsigned char (&bytes)[N])
{
    AnoGlslText<N> out = {};
    for (size_t i = 0; i < N; ++i)
        out.data[i] = static_cast<char>(bytes[i]);
    return out;
}

static constexpr unsigned char ano_textraster_comp_bytes[] = {
#embed "../../resources/shaders/textraster.comp"
, 0
};
static constexpr auto ano_textraster_comp_glsl = ano_gpu_glsl_text(ano_textraster_comp_bytes);

static constexpr unsigned char ano_textworld_vert_bytes[] = {
#embed "../../resources/shaders/textworld.vert"
, 0
};
static constexpr auto ano_textworld_vert_glsl = ano_gpu_glsl_text(ano_textworld_vert_bytes);

static constexpr unsigned char ano_textworld_frag_bytes[] = {
#embed "../../resources/shaders/textworld.frag"
, 0
};
static constexpr auto ano_textworld_frag_glsl = ano_gpu_glsl_text(ano_textworld_frag_bytes);

consteval uint32_t ano_gpu_align_up(uint32_t value, uint32_t alignment)
{
    return (value + alignment - 1u) & ~(alignment - 1u);
}

consteval AnoGpuStruct ano_gpu_parse(std::string_view source, std::string_view name, AnoGpuLayout layout);

consteval AnoGpuType ano_gpu_type(std::string_view source, std::string_view name, AnoGpuLayout layout)
{
    if (name == "float") return { AnoGpuScalar::f32, 1u, 4u, 4u };
    if (name == "int")   return { AnoGpuScalar::i32, 1u, 4u, 4u };
    if (name == "uint")  return { AnoGpuScalar::u32, 1u, 4u, 4u };
    if (name == "vec2")  return { AnoGpuScalar::f32, 2u, 8u, 8u };
    if (name == "ivec2") return { AnoGpuScalar::i32, 2u, 8u, 8u };
    if (name == "uvec2") return { AnoGpuScalar::u32, 2u, 8u, 8u };
    if (name == "vec3")  return { AnoGpuScalar::f32, 3u, 16u, 12u };
    if (name == "ivec3") return { AnoGpuScalar::i32, 3u, 16u, 12u };
    if (name == "uvec3") return { AnoGpuScalar::u32, 3u, 16u, 12u };
    if (name == "vec4")  return { AnoGpuScalar::f32, 4u, 16u, 16u };
    if (name == "ivec4") return { AnoGpuScalar::i32, 4u, 16u, 16u };
    if (name == "uvec4") return { AnoGpuScalar::u32, 4u, 16u, 16u };
    if (name == "mat4")  return { AnoGpuScalar::f32, 16u, 16u, 64u };
    const AnoGpuStruct nested = ano_gpu_parse(source, name, layout);
    if (!nested.valid || !nested.count)
        return {};
    AnoGpuScalar scalar = nested.fields[0].scalar;
    uint32_t components = 0;
    for (uint32_t i = 0; i < nested.count; ++i) {
        if (nested.fields[i].scalar != scalar)
            return {};
        components += nested.fields[i].components;
    }
    return { scalar, components, nested.alignment, nested.size };
}

consteval size_t ano_gpu_struct_body(std::string_view source, std::string_view wanted)
{
    AnoGpuCursor cursor = { source, 0 };
    while (cursor.at < source.size()) {
        const size_t before = cursor.at;
        const std::string_view token = cursor.identifier();
        if (token == "struct") {
            const std::string_view name = cursor.identifier();
            if (name == wanted && cursor.take('{'))
                return cursor.at;
        }
        if (cursor.at == before)
            ++cursor.at;
    }
    return std::string_view::npos;
}

// Finds "layout(... push_constant ...) uniform <wanted> {"; returns the offset just past '{'.
consteval size_t ano_gpu_push_body(std::string_view source, std::string_view wanted)
{
    AnoGpuCursor cursor = { source, 0 };
    while (cursor.at < source.size()) {
        const size_t before = cursor.at;
        if (cursor.identifier() == "layout" && cursor.take('(')) {
            bool push = false;
            for (;;) {
                cursor.skip();
                if (cursor.at >= source.size())
                    return std::string_view::npos;
                if (cursor.take(')'))
                    break;
                const std::string_view token = cursor.identifier();
                if (token == "push_constant")
                    push = true;
                if (token.empty())
                    ++cursor.at;
            }
            if (push && cursor.identifier() == "uniform" &&
                cursor.identifier() == wanted && cursor.take('{'))
                return cursor.at;
        }
        if (cursor.at == before)
            ++cursor.at;
    }
    return std::string_view::npos;
}

// in: body 〜 offset just past the opening '{'. named 〜 an instance identifier may precede ';'.
consteval AnoGpuStruct ano_gpu_parse_body(std::string_view source, size_t body, AnoGpuLayout layout, bool named)
{
    AnoGpuStruct result = {};
    AnoGpuCursor cursor = { source, body };
    uint32_t offset = 0;
    uint32_t structAlignment = layout == AnoGpuLayout::std140 ? 16u : 1u;
    for (;;) {
        cursor.skip();
        if (cursor.take('}'))
            break;
        if (result.count == static_cast<uint32_t>(sizeof(result.fields) / sizeof(result.fields[0])))
            return result;

        const AnoGpuType type = ano_gpu_type(source, cursor.identifier(), layout);
        const std::string_view fieldName = cursor.identifier();
        if (type.scalar == AnoGpuScalar::invalid || fieldName.empty())
            return result;

        uint32_t count = 1u;
        bool array = false;
        if (cursor.take('[')) {
            array = true;
            count = cursor.integer();
            if (!count || !cursor.take(']'))
                return result;
        }
        if (!cursor.take(';'))
            return result;

        uint32_t alignment = type.alignment;
        uint32_t fieldSize = type.size;
        if (array) {
            if (layout == AnoGpuLayout::std140 && alignment < 16u)
                alignment = 16u;
            fieldSize = ano_gpu_align_up(type.size, alignment) * count;
        }
        offset = ano_gpu_align_up(offset, alignment);
        result.fields[result.count++] = {
            fieldName, type.scalar, type.components * count, offset, fieldSize
        };
        offset += fieldSize;
        if (alignment > structAlignment)
            structAlignment = alignment;
    }

    result.alignment = structAlignment;
    result.size = ano_gpu_align_up(offset, structAlignment);
    if (named)
        cursor.identifier();
    result.valid = cursor.take(';');
    return result;
}

consteval AnoGpuStruct ano_gpu_parse(std::string_view source, std::string_view name, AnoGpuLayout layout)
{
    const size_t body = ano_gpu_struct_body(source, name);
    if (body == std::string_view::npos)
        return {};
    return ano_gpu_parse_body(source, body, layout, false);
}

template<class T>
consteval AnoGpuType ano_gpu_cpp_type()
{
    using U = std::remove_cv_t<T>;
    if constexpr (std::is_same_v<U, float>)
        return { AnoGpuScalar::f32, 1u, alignof(U), sizeof(U) };
    else if constexpr (std::is_same_v<U, int32_t>)
        return { AnoGpuScalar::i32, 1u, alignof(U), sizeof(U) };
    else if constexpr (std::is_same_v<U, uint32_t>)
        return { AnoGpuScalar::u32, 1u, alignof(U), sizeof(U) };
    else if constexpr (std::is_same_v<U, Vector2>)
        return { AnoGpuScalar::f32, 2u, alignof(U), sizeof(U) };
    else if constexpr (std::is_same_v<U, Vector3>)
        return { AnoGpuScalar::f32, 3u, alignof(U), sizeof(U) };
    else if constexpr (std::is_same_v<U, Vector4>)
        return { AnoGpuScalar::f32, 4u, alignof(U), sizeof(U) };
    else if constexpr (std::is_array_v<U>) {
        constexpr AnoGpuType element = ano_gpu_cpp_type<std::remove_extent_t<U>>();
        return {
            element.scalar,
            element.components * static_cast<uint32_t>(std::extent_v<U>),
            alignof(U),
            sizeof(U)
        };
    } else if constexpr (std::is_class_v<U>) {
        static constexpr auto members = std::define_static_array(
            std::meta::nonstatic_data_members_of(^^U, std::meta::access_context::unchecked()));
        AnoGpuScalar scalar = AnoGpuScalar::invalid;
        uint32_t components = 0;
        bool valid = true;
        template for (constexpr std::meta::info member : members) {
            using Member = [:std::meta::type_of(member):];
            constexpr AnoGpuType field = ano_gpu_cpp_type<Member>();
            if (scalar == AnoGpuScalar::invalid)
                scalar = field.scalar;
            valid = valid && field.scalar != AnoGpuScalar::invalid && field.scalar == scalar;
            components += field.components;
        }
        return valid ? AnoGpuType{ scalar, components, alignof(U), sizeof(U) } : AnoGpuType{};
    } else {
        return {};
    }
}

template<class T>
consteval bool ano_gpu_schema_matches(std::string_view source, std::string_view name,
                                      AnoGpuLayout layout = AnoGpuLayout::std430)
{
    if (!std::is_standard_layout_v<T> || !std::is_trivially_copyable_v<T> || std::is_polymorphic_v<T>)
        return false;

    static constexpr auto members = std::define_static_array(
        std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::unchecked()));
    const AnoGpuStruct gpu = ano_gpu_parse(source, name, layout);
    if (!gpu.valid || gpu.count != members.size() || gpu.size != sizeof(T))
        return false;

    uint32_t index = 0;
    bool valid = true;
    template for (constexpr std::meta::info member : members) {
        using Member = [:std::meta::type_of(member):];
        constexpr AnoGpuType cpu = ano_gpu_cpp_type<Member>();
        const AnoGpuField field = gpu.fields[index++];
        valid = valid &&
            std::meta::identifier_of(member) == field.name &&
            std::meta::offset_of(member).total_bits() == static_cast<uint64_t>(field.offset) * 8u &&
            cpu.scalar == field.scalar &&
            cpu.components == field.components &&
            cpu.size == field.size;
    }
    return valid;
}

// Push blocks flatten both sides to 4-byte scalar lanes 〜 GLSL may pack what C splits
// (uvec2 extent <-> extentW/extentH) and may declare only a prefix of the C struct.
// Every GLSL lane must match the C lane at the same index: same scalar, same byte offset.
template<class T>
consteval bool ano_gpu_push_matches(std::string_view source, std::string_view name)
{
    if (!std::is_standard_layout_v<T> || !std::is_trivially_copyable_v<T> || std::is_polymorphic_v<T>)
        return false;

    const size_t body = ano_gpu_push_body(source, name);
    if (body == std::string_view::npos)
        return false;
    const AnoGpuStruct gpu = ano_gpu_parse_body(source, body, AnoGpuLayout::std430, true);
    if (!gpu.valid || !gpu.count)
        return false;

    // GPU lanes; padded strides (field bytes != components * 4) are refused.
    AnoGpuScalar laneScalar[128] = {};
    uint32_t laneOffset[128] = {};
    uint32_t laneCount = 0;
    for (uint32_t i = 0; i < gpu.count; ++i) {
        const AnoGpuField field = gpu.fields[i];
        if (field.size != field.components * 4u ||
            laneCount + field.components > sizeof(laneScalar) / sizeof(laneScalar[0]))
            return false;
        for (uint32_t c = 0; c < field.components; ++c) {
            laneScalar[laneCount] = field.scalar;
            laneOffset[laneCount] = field.offset + c * 4u;
            ++laneCount;
        }
    }

    static constexpr auto members = std::define_static_array(
        std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::unchecked()));
    uint32_t lane = 0;
    bool valid = true;
    template for (constexpr std::meta::info member : members) {
        using Member = [:std::meta::type_of(member):];
        constexpr AnoGpuType cpu = ano_gpu_cpp_type<Member>();
        valid = valid && cpu.scalar != AnoGpuScalar::invalid && cpu.size == cpu.components * 4u;
        for (uint32_t c = 0; c < cpu.components; ++c, ++lane) {
            if (lane >= laneCount)
                continue;
            valid = valid &&
                laneScalar[lane] == cpu.scalar &&
                static_cast<uint64_t>(laneOffset[lane]) * 8u ==
                    std::meta::offset_of(member).total_bits() + static_cast<uint64_t>(c) * 32u;
        }
    }
    return valid && laneCount <= lane;
}

static_assert(ano_gpu_schema_matches<Vertex>(ano_gpu_abi_glsl, "PackedVertex"),
              "GPU ABI drift: Vertex <-> PackedVertex");
static_assert(ano_gpu_schema_matches<GpuEntityInfo>(ano_gpu_abi_glsl, "EntityInfo"),
              "GPU ABI drift: GpuEntityInfo <-> EntityInfo");
static_assert(ano_gpu_schema_matches<GpuMeshData>(ano_gpu_abi_glsl, "MeshData"),
              "GPU ABI drift: GpuMeshData <-> MeshData");
static_assert(ano_gpu_schema_matches<MaterialData>(ano_gpu_abi_glsl, "MaterialData"),
              "GPU ABI drift: MaterialData");
static_assert(ano_gpu_schema_matches<GlobalUBO>(
    ano_gpu_abi_glsl, "GlobalData", AnoGpuLayout::std140), "GPU ABI drift: GlobalUBO <-> GlobalData");
static_assert(ano_gpu_schema_matches<CullView>(ano_gpu_abi_glsl, "CullView"),
              "GPU ABI drift: CullView");
static_assert(ano_gpu_schema_matches<CullUBO>(
    ano_gpu_abi_glsl, "CullData", AnoGpuLayout::std140), "GPU ABI drift: CullUBO <-> CullData");
static_assert(ano_gpu_schema_matches<LightData>(ano_gpu_abi_glsl, "LightData"),
              "GPU ABI drift: LightData");
static_assert(ano_gpu_schema_matches<GpuLightRuntime>(ano_gpu_abi_glsl, "LightRuntime"),
              "GPU ABI drift: GpuLightRuntime <-> LightRuntime");
static_assert(ano_gpu_schema_matches<AnoInstanceData>(ano_gpu_abi_glsl, "InstanceData"),
              "GPU ABI drift: AnoInstanceData <-> InstanceData");
static_assert(ano_gpu_schema_matches<ShadowLightInfo>(ano_gpu_abi_glsl, "ShadowLightInfo"),
              "GPU ABI drift: ShadowLightInfo");
static_assert(ano_gpu_schema_matches<ShadowFrustumConfig>(ano_gpu_abi_glsl, "ShadowFrustumConfig"),
              "GPU ABI drift: ShadowFrustumConfig");
static_assert(ano_gpu_schema_matches<AnoMotionDescriptor>(ano_gpu_abi_glsl, "MotionDescriptor"),
              "GPU ABI drift: AnoMotionDescriptor <-> MotionDescriptor");
static_assert(ano_gpu_schema_matches<DecalRecord>(ano_gpu_abi_glsl, "DecalRecord"),
              "GPU ABI drift: DecalRecord");
static_assert(ano_gpu_schema_matches<SkinInstanceState>(ano_gpu_abi_glsl, "SkinInstanceState"),
              "GPU ABI drift: SkinInstanceState");
static_assert(ano_gpu_schema_matches<AnoGlyphEntry>(ano_text_abi_glsl, "GlyphEntry"),
              "GPU ABI drift: AnoGlyphEntry <-> GlyphEntry");
static_assert(ano_gpu_schema_matches<AnoGlyphInstance>(ano_text_abi_glsl, "GlyphInstance"),
              "GPU ABI drift: AnoGlyphInstance <-> GlyphInstance");
static_assert(ano_gpu_schema_matches<AnoUiPrim>(ano_ui_abi_glsl, "UiPrim"),
              "GPU ABI drift: AnoUiPrim <-> UiPrim");
static_assert(ano_gpu_schema_matches<AnoUiClip>(ano_ui_abi_glsl, "UiClip"),
              "GPU ABI drift: AnoUiClip <-> UiClip");
static_assert(ano_gpu_schema_matches<AnoUiPaint>(ano_ui_abi_glsl, "UiPaint"),
              "GPU ABI drift: AnoUiPaint <-> UiPaint");
static_assert(ano_gpu_schema_matches<AnoUiStop>(ano_ui_abi_glsl, "UiStop"),
              "GPU ABI drift: AnoUiStop <-> UiStop");
static_assert(ano_gpu_push_matches<TextRasterPush>(ano_textraster_comp_glsl.data, "TextRasterPush"),
              "GPU ABI drift: TextRasterPush <-> textraster.comp");
static_assert(ano_gpu_push_matches<TextWorldPush>(ano_textworld_vert_glsl.data, "TextWorldPush"),
              "GPU ABI drift: TextWorldPush <-> textworld.vert");
static_assert(ano_gpu_push_matches<TextWorldPush>(ano_textworld_frag_glsl.data, "TextWorldPush"),
              "GPU ABI drift: TextWorldPush <-> textworld.frag");

}
