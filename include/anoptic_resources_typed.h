/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// C++26 resource declarations compile directly into canonical operations.

#ifndef ANOPTICENGINE_ANOPTIC_RESOURCES_TYPED_H
#define ANOPTICENGINE_ANOPTIC_RESOURCES_TYPED_H

#ifndef __cplusplus
#error "anoptic_resources_typed.h requires C++26"
#endif

#include "anoptic_meta.h"
#include "anoptic_resources.h"

#include <bit>
#include <meta>
#include <stddef.h>
#include <stdint.h>
#include <string_view>
#include <type_traits>

namespace ano {

enum class Executor : uint8_t {
    io,
    worker,
    render_master,
    audio_master,
    text_owner,
};

enum class Streaming : uint8_t {
    whole,
    atoms,
};

struct Artifact final {};

struct Transform final {
    Executor executor;
    Streaming streaming;
    bool deterministic;
};

template<class SemanticAsset>
struct AssetRef final {
    AnoAssetId id;
};

template<class Element>
struct RelativeSpan final {
    uint64_t offset;
    uint64_t count;
};

// extent contains the live, aligned native objects addressed by value's RelativeSpan fields.
template<class ArtifactType>
struct ArtifactSource final {
    const ArtifactType *value;
    AnoResourceBytes extent;
};

template<class ArtifactType>
struct ArtifactView final {
    ArtifactType value;
    AnoResourceBytes bytes;
};

struct EncodeResult final {
    AnoResourceError error;
    uint64_t size;
};

template<class ArtifactType>
struct DecodeResult final {
    AnoResourceError error;
    ArtifactView<ArtifactType> view;
};

struct DependencyResult final {
    AnoResourceError error;
    uint64_t count;
};

namespace detail {

inline constexpr uint64_t canonicalHeaderSize = 64;
inline constexpr uint8_t canonicalMagic[8] = {'A', 'N', 'O', 'A', 'R', 'T', 0, 1};

constexpr uint32_t rotate_right(uint32_t value, uint32_t count)
{
    return (value >> count) | (value << (32u - count));
}

inline constexpr uint32_t sha256Constants[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u,
    0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
    0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
    0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
    0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
    0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
    0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u,
    0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
};

struct Sha256 final {
    uint32_t state[8] = {
        0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
        0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u,
    };
    uint8_t block[64] = {};
    uint64_t byteCount = 0;
    uint32_t blockSize = 0;

    constexpr void transform()
    {
        uint32_t words[64] = {};
        for (uint32_t i = 0; i < 16; ++i) {
            const uint32_t offset = i * 4;
            words[i] = (static_cast<uint32_t>(block[offset]) << 24)
                | (static_cast<uint32_t>(block[offset + 1]) << 16)
                | (static_cast<uint32_t>(block[offset + 2]) << 8)
                | static_cast<uint32_t>(block[offset + 3]);
        }
        for (uint32_t i = 16; i < 64; ++i) {
            const uint32_t s0 = rotate_right(words[i - 15], 7)
                ^ rotate_right(words[i - 15], 18) ^ (words[i - 15] >> 3);
            const uint32_t s1 = rotate_right(words[i - 2], 17)
                ^ rotate_right(words[i - 2], 19) ^ (words[i - 2] >> 10);
            words[i] = words[i - 16] + s0 + words[i - 7] + s1;
        }

        uint32_t a = state[0];
        uint32_t b = state[1];
        uint32_t c = state[2];
        uint32_t d = state[3];
        uint32_t e = state[4];
        uint32_t f = state[5];
        uint32_t g = state[6];
        uint32_t h = state[7];
        for (uint32_t i = 0; i < 64; ++i) {
            const uint32_t upperE = rotate_right(e, 6) ^ rotate_right(e, 11)
                ^ rotate_right(e, 25);
            const uint32_t choice = (e & f) ^ (~e & g);
            const uint32_t first = h + upperE + choice + sha256Constants[i] + words[i];
            const uint32_t upperA = rotate_right(a, 2) ^ rotate_right(a, 13)
                ^ rotate_right(a, 22);
            const uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
            const uint32_t second = upperA + majority;
            h = g;
            g = f;
            f = e;
            e = d + first;
            d = c;
            c = b;
            b = a;
            a = first + second;
        }
        state[0] += a;
        state[1] += b;
        state[2] += c;
        state[3] += d;
        state[4] += e;
        state[5] += f;
        state[6] += g;
        state[7] += h;
    }

    constexpr void append(uint8_t value)
    {
        block[blockSize++] = value;
        ++byteCount;
        if (blockSize == 64) {
            transform();
            blockSize = 0;
        }
    }

    constexpr void append(const uint8_t *bytes, uint64_t count)
    {
        for (uint64_t i = 0; i < count; ++i)
            append(bytes[i]);
    }

    constexpr void append(std::string_view text)
    {
        for (char value : text)
            append(static_cast<uint8_t>(value));
    }

    constexpr AnoContentId finish() const
    {
        Sha256 result = *this;
        const uint64_t bitCount = result.byteCount * 8;
        result.append(0x80);
        while (result.blockSize != 56)
            result.append(0);
        for (uint32_t i = 0; i < 8; ++i)
            result.append(static_cast<uint8_t>(bitCount >> ((7u - i) * 8u)));

        AnoContentId digest = {};
        for (uint32_t i = 0; i < 8; ++i) {
            digest.bytes[i * 4] = static_cast<uint8_t>(result.state[i] >> 24);
            digest.bytes[i * 4 + 1] = static_cast<uint8_t>(result.state[i] >> 16);
            digest.bytes[i * 4 + 2] = static_cast<uint8_t>(result.state[i] >> 8);
            digest.bytes[i * 4 + 3] = static_cast<uint8_t>(result.state[i]);
        }
        return digest;
    }
};

constexpr AnoContentId sha256(const uint8_t *bytes, uint64_t count)
{
    Sha256 hash;
    hash.append(bytes, count);
    return hash.finish();
}

constexpr void hash_u8(Sha256& hash, uint8_t value)
{
    hash.append(value);
}

constexpr void hash_u32(Sha256& hash, uint32_t value)
{
    for (uint32_t i = 0; i < 4; ++i)
        hash.append(static_cast<uint8_t>(value >> (i * 8u)));
}

constexpr void hash_u64(Sha256& hash, uint64_t value)
{
    for (uint32_t i = 0; i < 8; ++i)
        hash.append(static_cast<uint8_t>(value >> (i * 8u)));
}

[[noreturn]] consteval void reject(std::string_view message,
                                   std::meta::info declaration)
{
    (void)declaration;
#if __has_builtin(__builtin_constexpr_diag)
    __builtin_constexpr_diag(2, "", message.data());
#endif
    __builtin_abort();
}

consteval bool specialization_of(std::meta::info type,
                                 std::meta::info classTemplate)
{
    type = std::meta::dealias(type);
    return std::meta::has_template_arguments(type)
        && std::meta::template_of(type) == classTemplate;
}

enum class WireShape : uint8_t {
    invalid,
    boolean,
    integer,
    floating,
    array,
    assetRef,
    relativeSpan,
    record,
};

consteval bool has_artifact_marker(std::meta::info declaration)
{
    return !std::meta::annotations_of_with_type(declaration, ^^Artifact).empty();
}

consteval void validate_artifact_marker(std::meta::info declaration)
{
    const auto annotations =
        std::meta::annotations_of_with_type(declaration, ^^Artifact);
    if (annotations.size() != 1)
        reject("an artifact type requires exactly one Artifact annotation",
               declaration);
}

consteval auto wire_fields(std::meta::info type)
{
    auto fields = std::meta::nonstatic_data_members_of(
        type, std::meta::access_context::unchecked());
    for (const std::meta::info field : fields) {
        if (std::meta::is_bit_field(field))
            reject("canonical fields cannot be bit fields", field);
        if (!std::meta::has_identifier(field))
            reject("canonical fields require identifiers", field);
    }
    return fields;
}

consteval std::meta::info template_element(std::meta::info type)
{
    const auto arguments = std::meta::template_arguments_of(type);
    if (arguments.size() != 1 || !std::meta::is_type(arguments[0]))
        reject("resource wire templates require one type argument", type);
    return arguments[0];
}

consteval WireShape wire_shape(std::meta::info type)
{
    type = std::meta::dealias(type);
    if (type == ^^bool)
        return WireShape::boolean;
    if (std::meta::is_integral_type(type))
        return WireShape::integer;
    if (std::meta::is_floating_point_type(type))
        return WireShape::floating;
    if (std::meta::is_array_type(type))
        return WireShape::array;
    if (specialization_of(type, ^^AssetRef))
        return WireShape::assetRef;
    if (specialization_of(type, ^^RelativeSpan))
        return WireShape::relativeSpan;
    if (std::meta::is_class_type(type))
        return WireShape::record;
    return WireShape::invalid;
}

consteval bool wire_contains(std::meta::info type, WireShape sought)
{
    type = std::meta::dealias(type);
    const WireShape shape = wire_shape(type);
    if (shape == sought)
        return true;
    if (shape == WireShape::array)
        return wire_contains(std::meta::remove_extent(type), sought);
    if (shape == WireShape::relativeSpan)
        return wire_contains(template_element(type), sought);
    if (shape == WireShape::record)
        for (const std::meta::info field : wire_fields(type))
            if (wire_contains(std::meta::type_of(field), sought))
                return true;
    return false;
}

consteval void validate_wire_type(std::meta::info type,
                                  std::meta::info declaration)
{
    type = std::meta::dealias(type);
    const WireShape shape = wire_shape(type);
    if (shape == WireShape::invalid) {
        if (std::meta::is_pointer_type(type) || std::meta::is_reference_type(type)
            || std::meta::is_member_pointer_type(type))
            reject("portable artifacts cannot contain pointers or references",
                   declaration);
        if (std::meta::is_enum_type(type))
            reject("portable enum fields require an explicit reflected wire mapping",
                   declaration);
        reject("unsupported canonical field type", declaration);
    }

    if (shape == WireShape::boolean) {
        if (std::meta::size_of(type) != 1)
            reject("canonical bools must occupy one byte", declaration);
        return;
    }
    if (shape == WireShape::integer) {
        const size_t width = std::meta::size_of(type);
        if (width != 1 && width != 2 && width != 4 && width != 8)
            reject("canonical integers must occupy 1, 2, 4, or 8 bytes",
                   declaration);
        return;
    }
    if (shape == WireShape::floating) {
        const size_t width = std::meta::size_of(type);
        if (width != 4 && width != 8)
            reject("canonical floats must occupy 4 or 8 bytes", declaration);
        return;
    }

    if (shape == WireShape::array) {
        if (std::meta::rank(type) != 1 || std::meta::extent(type) == 0)
            reject("canonical arrays must have one fixed nonzero extent",
                   declaration);
        validate_wire_type(std::meta::remove_extent(type), declaration);
        return;
    }

    if (shape == WireShape::assetRef) {
        const std::meta::info target = template_element(type);
        if (!std::meta::is_complete_type(target))
            reject("AssetRef target types must be complete", declaration);
        validate_artifact_marker(target);
        return;
    }

    if (shape == WireShape::relativeSpan) {
        const std::meta::info element = template_element(type);
        if (!std::meta::is_complete_type(element))
            reject("RelativeSpan element types must be complete", declaration);
        validate_wire_type(element, declaration);
        return;
    }

    if (!std::meta::is_complete_type(type))
        reject("canonical records must be complete", declaration);
    if (!std::meta::is_standard_layout_type(type)
        || !std::meta::is_trivially_copyable_type(type)
        || !std::meta::is_aggregate_type(type)
        || !std::meta::is_final_type(type))
        reject("canonical records must be final standard-layout trivial aggregates",
               declaration);
    if (!std::meta::bases_of(type, std::meta::access_context::unchecked()).empty())
        reject("canonical records cannot have base classes", declaration);

    const auto fields = wire_fields(type);
    if (fields.empty())
        reject("canonical records must contain at least one field", declaration);
    for (const std::meta::info field : fields)
        validate_wire_type(std::meta::type_of(field), field);
}

consteval void validate_artifact(std::meta::info type)
{
    if (!std::meta::is_type(type) || !std::meta::is_complete_type(type))
        reject("artifact declarations must denote complete types", type);
    validate_artifact_marker(type);
    if (wire_shape(type) != WireShape::record)
        reject("artifact declarations must denote record types", type);
    validate_wire_type(type, type);
}

consteval void hash_qualified_name(Sha256& hash, std::meta::info declaration)
{
    if (std::meta::has_parent(declaration))
        hash_qualified_name(hash, std::meta::parent_of(declaration));
    if (!std::meta::has_identifier(declaration)) {
        if (std::meta::has_parent(declaration))
            reject("persistent declarations cannot use unnamed scopes", declaration);
        return;
    }
    const std::string_view identifier = std::meta::identifier_of(declaration);
    if (identifier.size() > UINT32_MAX)
        reject("declaration identifier exceeds canonical limits", declaration);
    hash_u32(hash, static_cast<uint32_t>(identifier.size()));
    hash.append(identifier);
}

consteval AnoResourceTypeId reflected_type_id(std::meta::info type)
{
    type = std::meta::dealias(type);
    if (!std::meta::is_type(type) || !std::meta::has_identifier(type))
        reject("resource type identity requires a named type", type);
    Sha256 hash;
    hash.append("anoptic.resource.type.v2");
    hash_qualified_name(hash, type);
    const AnoContentId digest = hash.finish();
    uint64_t value = 0;
    for (uint32_t i = 0; i < 8; ++i)
        value |= static_cast<uint64_t>(digest.bytes[i]) << (i * 8u);
    if (value == 0)
        reject("reflected resource type identity cannot be zero", type);
    return {value};
}

consteval uint64_t checked_wire_add(uint64_t lhs, uint64_t rhs,
                                    std::meta::info declaration)
{
    if (rhs > UINT64_MAX - lhs)
        reject("canonical fixed size overflows uint64_t", declaration);
    return lhs + rhs;
}

consteval uint64_t checked_wire_multiply(uint64_t lhs, uint64_t rhs,
                                         std::meta::info declaration)
{
    if (lhs != 0 && rhs > UINT64_MAX / lhs)
        reject("canonical fixed size overflows uint64_t", declaration);
    return lhs * rhs;
}

consteval uint64_t wire_size(std::meta::info type)
{
    type = std::meta::dealias(type);
    const WireShape shape = wire_shape(type);
    if (shape == WireShape::boolean || shape == WireShape::integer
        || shape == WireShape::floating)
        return std::meta::size_of(type);
    if (shape == WireShape::array)
        return checked_wire_multiply(std::meta::extent(type),
                                     wire_size(std::meta::remove_extent(type)), type);
    if (shape == WireShape::assetRef)
        return 8;
    if (shape == WireShape::relativeSpan)
        return 16;
    if (shape == WireShape::record) {
        uint64_t result = 0;
        for (const std::meta::info field : wire_fields(type))
            result = checked_wire_add(result,
                                      wire_size(std::meta::type_of(field)), field);
        return result;
    }
    reject("unsupported canonical wire type", type);
}

consteval void hash_wire_type(Sha256& hash, std::meta::info type)
{
    type = std::meta::dealias(type);
    const WireShape shape = wire_shape(type);
    if (shape == WireShape::boolean) {
        hash_u8(hash, 'b');
        return;
    }
    if (shape == WireShape::integer) {
        hash_u8(hash, 'i');
        hash_u8(hash, static_cast<uint8_t>(std::meta::size_of(type)));
        hash_u8(hash, std::meta::is_signed_type(type) ? 1 : 0);
        return;
    }
    if (shape == WireShape::floating) {
        hash_u8(hash, 'f');
        hash_u8(hash, static_cast<uint8_t>(std::meta::size_of(type)));
        return;
    }
    if (shape == WireShape::array) {
        hash_u8(hash, 'a');
        hash_u64(hash, std::meta::extent(type));
        hash_wire_type(hash, std::meta::remove_extent(type));
        return;
    }
    if (shape == WireShape::assetRef) {
        hash_u8(hash, 'd');
        hash_qualified_name(hash, template_element(type));
        return;
    }
    if (shape == WireShape::relativeSpan) {
        hash_u8(hash, 's');
        hash_wire_type(hash, template_element(type));
        return;
    }

    if (shape != WireShape::record)
        reject("unsupported canonical wire type", type);
    hash_u8(hash, 'r');
    hash_qualified_name(hash, type);
    const auto fields = wire_fields(type);
    hash_u32(hash, static_cast<uint32_t>(fields.size()));
    for (const std::meta::info field : fields) {
        const std::string_view identifier = std::meta::identifier_of(field);
        hash_u32(hash, static_cast<uint32_t>(identifier.size()));
        hash.append(identifier);
        hash_wire_type(hash, std::meta::type_of(field));
    }
}

consteval AnoSchemaFingerprint fingerprint(std::meta::info type)
{
    validate_artifact(type);
    Sha256 hash;
    hash.append("anoptic.resource.schema.v2");
    hash_qualified_name(hash, type);
    hash_wire_type(hash, type);
    const AnoContentId content = hash.finish();
    AnoSchemaFingerprint result = {};
    for (size_t i = 0; i < sizeof(result.bytes); ++i)
        result.bytes[i] = content.bytes[i];
    return result;
}

template<class Type>
consteval auto fields_for()
{
    return std::define_static_array(wire_fields(^^Type));
}

template<class Type>
consteval bool require_artifact()
{
    validate_artifact(^^Type);
    return true;
}

template<class Type>
inline constexpr AnoSchemaFingerprint compiledFingerprint = fingerprint(^^Type);

template<class Type>
inline constexpr uint64_t compiledWireSize = wire_size(^^Type);

consteval void validate_transform(std::meta::info declaration)
{
    const auto annotations =
        std::meta::annotations_of_with_type(declaration, ^^Transform);
    if (annotations.size() != 1)
        reject("a transform requires exactly one Transform annotation", declaration);
    if (!std::meta::is_function(declaration) || !std::meta::is_noexcept(declaration))
        reject("resource transforms must be noexcept functions", declaration);
    if (std::meta::return_type_of(declaration) != ^^bool)
        reject("resource transforms return bool", declaration);
    const auto parameters = std::meta::parameters_of(declaration);
    std::meta::info input{};
    std::meta::info output{};
    for (const std::meta::info parameter : parameters) {
        const std::meta::info parameterType = std::meta::type_of(parameter);
        const std::meta::info valueType = std::meta::remove_cvref(parameterType);
        if (!has_artifact_marker(valueType))
            continue;
        validate_artifact(valueType);
        if (!std::meta::is_lvalue_reference_type(parameterType))
            reject("artifact transform parameters must be lvalue references",
                   parameter);
        const std::meta::info referred = std::meta::remove_reference(parameterType);
        if (std::meta::is_const_type(referred)) {
            if (input != std::meta::info{})
                reject("resource transforms require exactly one artifact input",
                       parameter);
            input = valueType;
        } else {
            if (output != std::meta::info{})
                reject("resource transforms require exactly one artifact output",
                       parameter);
            output = valueType;
        }
    }
    if (input == std::meta::info{} || output == std::meta::info{})
        reject("resource transforms require one const artifact input and one artifact output",
               declaration);
}

constexpr bool checked_add(uint64_t lhs, uint64_t rhs, uint64_t *result)
{
    if (result == nullptr || rhs > UINT64_MAX - lhs)
        return false;
    *result = lhs + rhs;
    return true;
}

constexpr bool checked_multiply(uint64_t lhs, uint64_t rhs, uint64_t *result)
{
    if (result == nullptr || (lhs != 0 && rhs > UINT64_MAX / lhs))
        return false;
    *result = lhs * rhs;
    return true;
}

constexpr bool byte_range(uint64_t size, uint64_t offset, uint64_t count)
{
    return offset <= size && count <= size - offset;
}

constexpr bool fingerprint_equal(const AnoSchemaFingerprint& lhs,
                                 const AnoSchemaFingerprint& rhs)
{
    for (size_t i = 0; i < sizeof(lhs.bytes); ++i)
        if (lhs.bytes[i] != rhs.bytes[i])
            return false;
    return true;
}

constexpr uint64_t read_unsigned(const uint8_t *bytes, uint32_t width)
{
    uint64_t value = 0;
    for (uint32_t i = 0; i < width; ++i)
        value |= static_cast<uint64_t>(bytes[i]) << (i * 8u);
    return value;
}

constexpr void write_unsigned(uint8_t *bytes, uint64_t value, uint32_t width)
{
    for (uint32_t i = 0; i < width; ++i)
        bytes[i] = static_cast<uint8_t>(value >> (i * 8u));
}

template<class Type>
inline constexpr WireShape compiledWireShape = wire_shape(^^Type);

struct PlanContext final {
    AnoResourceBytes source;
    uint64_t payloadCursor;
    AnoResourceError error;
};

template<class Type>
constexpr const Type *source_elements(AnoResourceBytes source,
                                      RelativeSpan<Type> span,
                                      AnoResourceError *error)
{
    if (error == nullptr || *error != ANO_RESOURCE_OK)
        return nullptr;
    if (span.count == 0) {
        if (span.offset != 0)
            *error = ANO_RESOURCE_NON_CANONICAL;
        return nullptr;
    }
    uint64_t byteCount = 0;
    if (!checked_multiply(span.count, sizeof(Type), &byteCount)) {
        *error = ANO_RESOURCE_OVERFLOW;
        return nullptr;
    }
    if (source.data == nullptr || !byte_range(source.size, span.offset, byteCount)) {
        *error = ANO_RESOURCE_OUT_OF_BOUNDS;
        return nullptr;
    }
    const uint8_t *address = source.data + span.offset;
    if (reinterpret_cast<uintptr_t>(address) % alignof(Type) != 0) {
        *error = ANO_RESOURCE_MISALIGNED_SOURCE;
        return nullptr;
    }
    return reinterpret_cast<const Type *>(address);
}

template<class Type>
constexpr void plan_value(const Type& value, PlanContext& context);

template<class Type>
constexpr void plan_record(const Type& value, PlanContext& context)
{
    static constexpr auto fields = fields_for<Type>();
    template for (constexpr std::meta::info field : fields) {
        if (context.error == ANO_RESOURCE_OK)
            plan_value(value.[:field:], context);
    }
}

template<class Type>
constexpr void plan_value(const Type& value, PlanContext& context)
{
    if (context.error != ANO_RESOURCE_OK)
        return;
    if constexpr (compiledWireShape<Type> == WireShape::relativeSpan) {
        constexpr std::meta::info elementInfo = template_element(^^Type);
        using Element = [:elementInfo:];
        const Element *elements = source_elements(context.source, value,
                                                  &context.error);
        if (context.error != ANO_RESOURCE_OK || value.count == 0)
            return;
        uint64_t fixedBytes = 0;
        if (!checked_multiply(value.count, wire_size(^^Element), &fixedBytes)
            || !checked_add(context.payloadCursor, fixedBytes,
                            &context.payloadCursor)) {
            context.error = ANO_RESOURCE_OVERFLOW;
            return;
        }
        for (uint64_t i = 0; i < value.count; ++i)
            plan_value(elements[i], context);
    } else if constexpr (compiledWireShape<Type> == WireShape::array) {
        for (size_t i = 0; i < std::extent_v<Type>; ++i)
            plan_value(value[i], context);
    } else if constexpr (compiledWireShape<Type> == WireShape::record) {
        plan_record(value, context);
    }
}

struct EncodeContext final {
    AnoResourceBytes source;
    AnoResourceMutableBytes output;
    uint64_t payloadCursor;
    AnoResourceError error;
};

template<class Type>
constexpr void encode_value(const Type& value, uint64_t offset,
                            EncodeContext& context);

template<class Type>
constexpr void encode_record(const Type& value, uint64_t offset,
                             EncodeContext& context)
{
    static constexpr auto fields = fields_for<Type>();
    uint64_t cursor = offset;
    template for (constexpr std::meta::info field : fields) {
        using FieldType = [:std::meta::type_of(field):];
        if (context.error == ANO_RESOURCE_OK)
            encode_value(value.[:field:], cursor, context);
        cursor += wire_size(^^FieldType);
    }
}

template<class Type>
constexpr void encode_value(const Type& value, uint64_t offset,
                            EncodeContext& context)
{
    if (context.error != ANO_RESOURCE_OK)
        return;
    if constexpr (compiledWireShape<Type> == WireShape::assetRef) {
        write_unsigned(context.output.data + offset, value.id.value, 8);
    } else if constexpr (compiledWireShape<Type> == WireShape::relativeSpan) {
        constexpr std::meta::info elementInfo = template_element(^^Type);
        using Element = [:elementInfo:];
        if (value.count == 0) {
            write_unsigned(context.output.data + offset, 0, 8);
            write_unsigned(context.output.data + offset + 8, 0, 8);
            return;
        }
        const Element *elements = source_elements(context.source, value,
                                                  &context.error);
        if (context.error != ANO_RESOURCE_OK)
            return;
        const uint64_t payloadOffset = context.payloadCursor;
        uint64_t fixedBytes = 0;
        if (!checked_multiply(value.count, wire_size(^^Element), &fixedBytes)
            || !checked_add(context.payloadCursor, fixedBytes,
                            &context.payloadCursor)) {
            context.error = ANO_RESOURCE_OVERFLOW;
            return;
        }
        write_unsigned(context.output.data + offset, payloadOffset, 8);
        write_unsigned(context.output.data + offset + 8, value.count, 8);
        for (uint64_t i = 0; i < value.count; ++i)
            encode_value(elements[i],
                         payloadOffset + i * wire_size(^^Element), context);
    } else if constexpr (compiledWireShape<Type> == WireShape::array) {
        using Element = std::remove_extent_t<Type>;
        for (size_t i = 0; i < std::extent_v<Type>; ++i)
            encode_value(value[i], offset + i * wire_size(^^Element), context);
    } else if constexpr (compiledWireShape<Type> == WireShape::boolean) {
        context.output.data[offset] = value ? uint8_t{1} : uint8_t{0};
    } else if constexpr (compiledWireShape<Type> == WireShape::integer) {
        using Unsigned = std::make_unsigned_t<Type>;
        const Unsigned bits = std::bit_cast<Unsigned>(value);
        write_unsigned(context.output.data + offset,
                       static_cast<uint64_t>(bits), sizeof(Type));
    } else if constexpr (compiledWireShape<Type> == WireShape::floating) {
        if constexpr (sizeof(Type) == 4) {
            write_unsigned(context.output.data + offset,
                           std::bit_cast<uint32_t>(value), 4);
        } else {
            write_unsigned(context.output.data + offset,
                           std::bit_cast<uint64_t>(value), 8);
        }
    } else if constexpr (compiledWireShape<Type> == WireShape::record) {
        encode_record(value, offset, context);
    } else {
        static_assert(compiledWireShape<Type> != WireShape::invalid,
                      "unsupported canonical wire type");
    }
}

struct DecodeContext final {
    AnoResourceBytes bytes;
    uint64_t payloadCursor;
    AnoAssetId *dependencies;
    uint64_t dependencyCapacity;
    uint64_t dependencyCount;
    bool collectDependencies;
    AnoResourceError error;
};

template<class Type>
constexpr void decode_value(uint64_t offset, DecodeContext& context,
                            Type *output);

template<class Type>
constexpr void decode_record(uint64_t offset, DecodeContext& context,
                             Type *output)
{
    static constexpr auto fields = fields_for<Type>();
    uint64_t cursor = offset;
    template for (constexpr std::meta::info field : fields) {
        using FieldType = [:std::meta::type_of(field):];
        FieldType *fieldOutput = output == nullptr ? nullptr : &output->[:field:];
        if (context.error == ANO_RESOURCE_OK)
            decode_value(cursor, context, fieldOutput);
        cursor += wire_size(^^FieldType);
    }
}

template<class Type>
constexpr void decode_value(uint64_t offset, DecodeContext& context,
                            Type *output)
{
    if (context.error != ANO_RESOURCE_OK)
        return;
    constexpr uint64_t fixedSize = wire_size(^^Type);
    if (!byte_range(context.bytes.size, offset, fixedSize)) {
        context.error = ANO_RESOURCE_TRUNCATED;
        return;
    }

    if constexpr (compiledWireShape<Type> == WireShape::assetRef) {
        const AnoAssetId id = {read_unsigned(context.bytes.data + offset, 8)};
        if (output != nullptr)
            output->id = id;
        if (context.collectDependencies && id.value != 0) {
            if (context.dependencies != nullptr
                && context.dependencyCount < context.dependencyCapacity)
                context.dependencies[context.dependencyCount] = id;
            ++context.dependencyCount;
        }
    } else if constexpr (compiledWireShape<Type> == WireShape::relativeSpan) {
        constexpr std::meta::info elementInfo = template_element(^^Type);
        using Element = [:elementInfo:];
        const uint64_t spanOffset = read_unsigned(context.bytes.data + offset, 8);
        const uint64_t count = read_unsigned(context.bytes.data + offset + 8, 8);
        if (count == 0) {
            if (spanOffset != 0)
                context.error = ANO_RESOURCE_NON_CANONICAL;
            else if (output != nullptr)
                *output = {0, 0};
            return;
        }
        if (spanOffset != context.payloadCursor) {
            context.error = ANO_RESOURCE_NON_CANONICAL;
            return;
        }
        uint64_t fixedBytes = 0;
        if (!checked_multiply(count, wire_size(^^Element), &fixedBytes)
            || !byte_range(context.bytes.size, spanOffset, fixedBytes)
            || !checked_add(context.payloadCursor, fixedBytes,
                            &context.payloadCursor)) {
            context.error = ANO_RESOURCE_OUT_OF_BOUNDS;
            return;
        }
        if (output != nullptr)
            *output = {spanOffset, count};
        for (uint64_t i = 0; i < count; ++i)
            decode_value(spanOffset + i * wire_size(^^Element), context,
                         static_cast<Element *>(nullptr));
    } else if constexpr (compiledWireShape<Type> == WireShape::array) {
        using Element = std::remove_extent_t<Type>;
        for (size_t i = 0; i < std::extent_v<Type>; ++i)
            decode_value(offset + i * wire_size(^^Element), context,
                         output == nullptr ? nullptr : &(*output)[i]);
    } else if constexpr (compiledWireShape<Type> == WireShape::boolean) {
        const uint8_t value = context.bytes.data[offset];
        if (value > 1) {
            context.error = ANO_RESOURCE_NON_CANONICAL;
            return;
        }
        if (output != nullptr)
            *output = value != 0;
    } else if constexpr (compiledWireShape<Type> == WireShape::integer) {
        using Unsigned = std::make_unsigned_t<Type>;
        const Unsigned bits = static_cast<Unsigned>(
            read_unsigned(context.bytes.data + offset, sizeof(Type)));
        if (output != nullptr)
            *output = std::bit_cast<Type>(bits);
    } else if constexpr (compiledWireShape<Type> == WireShape::floating) {
        if (output != nullptr) {
            if constexpr (sizeof(Type) == 4)
                *output = std::bit_cast<Type>(static_cast<uint32_t>(
                    read_unsigned(context.bytes.data + offset, 4)));
            else
                *output = std::bit_cast<Type>(
                    read_unsigned(context.bytes.data + offset, 8));
        }
    } else if constexpr (compiledWireShape<Type> == WireShape::record) {
        decode_record(offset, context, output);
    } else {
        static_assert(compiledWireShape<Type> != WireShape::invalid,
                      "unsupported canonical wire type");
    }
}

template<class Type>
constexpr AnoResourceError decode_artifact(AnoResourceBytes bytes, Type *output,
                                           AnoAssetId *dependencies,
                                           uint64_t dependencyCapacity,
                                           uint64_t *dependencyCount,
                                           bool collectDependencies)
{
    static_assert(require_artifact<Type>());
    if ((bytes.data == nullptr && bytes.size != 0) || dependencyCount == nullptr)
        return ANO_RESOURCE_INVALID_ARGUMENT;
    if (bytes.size < canonicalHeaderSize)
        return ANO_RESOURCE_TRUNCATED;
    for (size_t i = 0; i < sizeof(canonicalMagic); ++i)
        if (bytes.data[i] != canonicalMagic[i])
            return ANO_RESOURCE_BAD_MAGIC;

    constexpr AnoResourceTypeId type = reflected_type_id(^^Type);
    if (read_unsigned(bytes.data + 8, 8) != type.value)
        return ANO_RESOURCE_TYPE_MISMATCH;
    constexpr AnoSchemaFingerprint expected = compiledFingerprint<Type>;
    AnoSchemaFingerprint encoded = {};
    for (size_t i = 0; i < sizeof(encoded.bytes); ++i)
        encoded.bytes[i] = bytes.data[16 + i];
    if (!fingerprint_equal(expected, encoded))
        return ANO_RESOURCE_SCHEMA_MISMATCH;

    const uint64_t rootSize = read_unsigned(bytes.data + 48, 8);
    const uint64_t totalSize = read_unsigned(bytes.data + 56, 8);
    if (rootSize != compiledWireSize<Type> || totalSize != bytes.size)
        return ANO_RESOURCE_NON_CANONICAL;
    uint64_t payloadStart = 0;
    if (!checked_add(canonicalHeaderSize, rootSize, &payloadStart)
        || payloadStart > bytes.size)
        return ANO_RESOURCE_TRUNCATED;

    DecodeContext context = {
        .bytes = bytes,
        .payloadCursor = payloadStart,
        .dependencies = dependencies,
        .dependencyCapacity = dependencyCapacity,
        .dependencyCount = 0,
        .collectDependencies = collectDependencies,
        .error = ANO_RESOURCE_OK,
    };
    decode_value(canonicalHeaderSize, context, output);
    *dependencyCount = context.dependencyCount;
    if (context.error != ANO_RESOURCE_OK)
        return context.error;
    if (context.payloadCursor != bytes.size)
        return ANO_RESOURCE_NON_CANONICAL;
    if (collectDependencies && context.dependencyCount > dependencyCapacity)
        return ANO_RESOURCE_DEPENDENCY_CAPACITY;
    return ANO_RESOURCE_OK;
}

} // namespace detail

consteval bool compile_resource_language(std::meta::info schemaNamespace)
{
    if (!std::meta::is_namespace(schemaNamespace))
        detail::reject("resource language input must be a namespace",
                       schemaNamespace);
    const auto declarations = std::meta::members_of(
        schemaNamespace, std::meta::access_context::unchecked());
    size_t artifactCount = 0;
    for (size_t i = 0; i < declarations.size(); ++i) {
        const std::meta::info declaration = declarations[i];
        const auto transformAnnotations =
            std::meta::annotations_of_with_type(declaration, ^^Transform);
        if (detail::has_artifact_marker(declaration)) {
            detail::validate_artifact(declaration);
            const AnoResourceTypeId type = detail::reflected_type_id(declaration);
            for (size_t j = 0; j < i; ++j)
                if (detail::has_artifact_marker(declarations[j])
                    && detail::reflected_type_id(declarations[j]).value == type.value)
                    detail::reject("reflected resource type identity collision",
                                   declaration);
            ++artifactCount;
        }
        if (!transformAnnotations.empty())
            detail::validate_transform(declaration);
    }

    if (artifactCount == 0)
        detail::reject("resource language contains no artifact declarations",
                       schemaNamespace);
    return true;
}

template<class Type>
consteval AnoResourceTypeId resource_type_id()
{
    detail::validate_artifact(^^Type);
    return detail::reflected_type_id(^^Type);
}

template<class Type>
consteval AnoSchemaFingerprint schema_fingerprint()
{
    return detail::compiledFingerprint<Type>;
}

template<class Type>
consteval uint64_t fixed_wire_size()
{
    detail::validate_wire_type(^^Type, ^^Type);
    return detail::compiledWireSize<Type>;
}

template<class Type>
constexpr EncodeResult encoded_size(ArtifactSource<Type> source)
{
    static_assert(detail::require_artifact<Type>());
    if (source.value == nullptr
        || (source.extent.data == nullptr && source.extent.size != 0))
        return {ANO_RESOURCE_INVALID_ARGUMENT, 0};
    uint64_t initial = 0;
    if (!detail::checked_add(detail::canonicalHeaderSize,
                             detail::compiledWireSize<Type>, &initial))
        return {ANO_RESOURCE_OVERFLOW, 0};
    detail::PlanContext context = {
        .source = source.extent,
        .payloadCursor = initial,
        .error = ANO_RESOURCE_OK,
    };
    detail::plan_value(*source.value, context);
    return {context.error, context.payloadCursor};
}

template<class Type>
constexpr EncodeResult encode(ArtifactSource<Type> source,
                              AnoResourceMutableBytes output)
{
    const EncodeResult measured = encoded_size(source);
    if (measured.error != ANO_RESOURCE_OK)
        return measured;
    if (output.data == nullptr || output.size < measured.size)
        return {ANO_RESOURCE_BUFFER_TOO_SMALL, measured.size};

    for (size_t i = 0; i < sizeof(detail::canonicalMagic); ++i)
        output.data[i] = detail::canonicalMagic[i];
    constexpr AnoResourceTypeId type = detail::reflected_type_id(^^Type);
    detail::write_unsigned(output.data + 8, type.value, 8);
    constexpr AnoSchemaFingerprint schema = detail::compiledFingerprint<Type>;
    for (size_t i = 0; i < sizeof(schema.bytes); ++i)
        output.data[16 + i] = schema.bytes[i];
    detail::write_unsigned(output.data + 48, detail::compiledWireSize<Type>, 8);
    detail::write_unsigned(output.data + 56, measured.size, 8);

    uint64_t payloadStart = 0;
    (void)detail::checked_add(detail::canonicalHeaderSize,
                              detail::compiledWireSize<Type>, &payloadStart);
    detail::EncodeContext context = {
        .source = source.extent,
        .output = output,
        .payloadCursor = payloadStart,
        .error = ANO_RESOURCE_OK,
    };
    detail::encode_value(*source.value, detail::canonicalHeaderSize, context);
    if (context.error != ANO_RESOURCE_OK)
        return {context.error, measured.size};
    if (context.payloadCursor != measured.size)
        return {ANO_RESOURCE_NON_CANONICAL, measured.size};
    return {ANO_RESOURCE_OK, measured.size};
}

template<class Type>
constexpr AnoResourceError validate(AnoResourceBytes bytes)
{
    uint64_t dependencyCount = 0;
    return detail::decode_artifact<Type>(bytes, nullptr, nullptr, 0,
                                         &dependencyCount, false);
}

template<class Type>
constexpr DecodeResult<Type> decode(AnoResourceBytes bytes)
{
    DecodeResult<Type> result = {
        .error = ANO_RESOURCE_OK,
        .view = {.value = {}, .bytes = bytes},
    };
    uint64_t dependencyCount = 0;
    result.error = detail::decode_artifact<Type>(
        bytes, &result.view.value, nullptr, 0, &dependencyCount, false);
    return result;
}

template<class Type>
constexpr DependencyResult dependencies(AnoResourceBytes bytes,
                                        AnoAssetId *output,
                                        uint64_t capacity)
{
    if (output == nullptr && capacity != 0)
        return {ANO_RESOURCE_INVALID_ARGUMENT, 0};
    uint64_t count = 0;
    const AnoResourceError error = detail::decode_artifact<Type>(
        bytes, nullptr, output, capacity, &count, true);
    return {error, count};
}

template<class Root, class Element>
constexpr AnoResourceError resolve(const ArtifactView<Root>& view,
                                   RelativeSpan<Element> span, uint64_t index,
                                   Element *output)
{
    static_assert(!detail::wire_contains(^^Element, detail::WireShape::relativeSpan),
                  "resolve returns fixed elements; nested spans remain views");
    if (output == nullptr)
        return ANO_RESOURCE_INVALID_ARGUMENT;
    const AnoResourceError valid = validate<Root>(view.bytes);
    if (valid != ANO_RESOURCE_OK)
        return valid;
    if (index >= span.count)
        return ANO_RESOURCE_OUT_OF_BOUNDS;
    uint64_t relative = 0;
    uint64_t offset = 0;
    if (!detail::checked_multiply(index, detail::wire_size(^^Element), &relative)
        || !detail::checked_add(span.offset, relative, &offset)
        || !detail::byte_range(view.bytes.size, offset,
                               detail::wire_size(^^Element)))
        return ANO_RESOURCE_OUT_OF_BOUNDS;
    detail::DecodeContext context = {
        .bytes = view.bytes,
        .payloadCursor = view.bytes.size,
        .dependencies = nullptr,
        .dependencyCapacity = 0,
        .dependencyCount = 0,
        .collectDependencies = false,
        .error = ANO_RESOURCE_OK,
    };
    detail::decode_value(offset, context, output);
    return context.error;
}

static_assert([] {
    uint64_t value = 0;
    return detail::checked_add(4, 5, &value) && value == 9
        && !detail::checked_add(UINT64_MAX, 1, &value);
}());
static_assert([] {
    uint64_t value = 0;
    return detail::checked_multiply(7, 9, &value) && value == 63
        && !detail::checked_multiply(UINT64_MAX, 2, &value);
}());

} // namespace ano

#endif // ANOPTICENGINE_ANOPTIC_RESOURCES_TYPED_H
