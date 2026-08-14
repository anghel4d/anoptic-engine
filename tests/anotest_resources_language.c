/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0
 * Anoptic targets ISO C++26. */
/*  == Anoptic Game Engine v0.0000001 == */

// Reflected schemas, canonical artifacts, hostile-byte rejection, and SHA-256.

#include <anoptic_render_resources.h>

using namespace ano;
#include <anoptic_resources.h>

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

using ano::asset_schema::Material;
using ano::asset_schema::MaterialAlphaMode;
using ano::asset_schema::MaterialTextureSlot;
using ano::asset_schema::Mesh;
using ano::asset_schema::Scene;
using ano::asset_schema::SceneLight;
using ano::asset_schema::SceneRenderable;
using ano::asset_schema::Texture;
using ano::asset_schema::Vertex;

static int failures = 0;

#define CHECK(condition, message) do { \
    if (!(condition)) { \
        printf("FAIL: %s (%s:%d)\n", (message), __FILE__, __LINE__); \
        ++failures; \
    } \
} while (0)

constexpr bool fingerprint_equal(AnoSchemaFingerprint lhs,
                                 AnoSchemaFingerprint rhs)
{
    for (size_t i = 0; i < sizeof(lhs.bytes); ++i)
        if (lhs.bytes[i] != rhs.bytes[i])
            return false;
    return true;
}

namespace reflected_schema_probe {

struct [[=ano::Artifact{}]] First final {
    uint32_t first;
    uint64_t second;
};

struct [[=ano::Artifact{}]] Reordered final {
    uint64_t second;
    uint32_t first;
};

} // namespace reflected_schema_probe

namespace reflected_transform_probe {

struct [[=ano::Artifact{}]] Source final {
    uint32_t value;
};

struct [[=ano::Artifact{}]] Output final {
    uint32_t value;
};

[[=ano::Transform{ano::Executor::worker, ano::Streaming::whole, true}]]
bool transform(const Source&, uint32_t scratchSize, Output&) noexcept;

} // namespace reflected_transform_probe

static_assert(ano::compile_resource_language(^^ano::asset_schema));
static_assert(ano::compile_resource_language(^^reflected_transform_probe));
static_assert(ano::fixed_wire_size<Texture>() == 30);
static_assert(ano::fixed_wire_size<Material>() == 1037);
static_assert(ano::fixed_wire_size<Vertex>() == 32);
static_assert(ano::fixed_wire_size<Mesh>() == 64);
static_assert(ano::fixed_wire_size<SceneRenderable>() == 72);
static_assert(ano::fixed_wire_size<SceneLight>() == 94);
static_assert(ano::fixed_wire_size<Scene>() == 32);
static_assert(ano::resource_type_id<Texture>().value != 0);
static_assert(ano::resource_type_id<Texture>().value
              != ano::resource_type_id<Material>().value);
static_assert(ano::resource_type_id<reflected_schema_probe::First>().value
              != ano::resource_type_id<reflected_schema_probe::Reordered>().value);
static_assert(!fingerprint_equal(
    ano::schema_fingerprint<reflected_schema_probe::First>(),
    ano::schema_fingerprint<reflected_schema_probe::Reordered>()));

constexpr uint8_t abcBytes[3] = {'a', 'b', 'c'};
constexpr AnoContentId abcDigest = ano::detail::sha256(abcBytes, sizeof(abcBytes));
static_assert(abcDigest.bytes[0] == 0xba && abcDigest.bytes[1] == 0x78
              && abcDigest.bytes[30] == 0x15 && abcDigest.bytes[31] == 0xad);

struct ShaPattern final { uint8_t bytes[257]; };
struct ShaCases final { AnoContentId values[10]; };

consteval ShaPattern make_sha_pattern()
{
    ShaPattern pattern = {};
    for (uint32_t i = 0; i < sizeof(pattern.bytes); ++i)
        pattern.bytes[i] = static_cast<uint8_t>(i * 29u + 17u);
    return pattern;
}

inline constexpr ShaPattern shaPattern = make_sha_pattern();
inline constexpr uint64_t shaLengths[10] = {
    0, 1, 55, 56, 63, 64, 65, 127, 128, sizeof(shaPattern.bytes),
};

consteval ShaCases make_sha_cases()
{
    ShaCases cases = {};
    for (uint32_t i = 0; i < 10; ++i)
        cases.values[i] = ano::detail::sha256(
            shaPattern.bytes, shaLengths[i]);
    return cases;
}

inline constexpr ShaCases shaCases = make_sha_cases();

constexpr Material make_material(void)
{
    Material material = {};
    material.baseColorFactor[0] = 1.0f;
    material.baseColorFactor[1] = 0.5f;
    material.baseColorFactor[2] = 0.25f;
    material.baseColorFactor[3] = 1.0f;
    material.emissiveFactor[0] = 0.1f;
    material.emissiveFactor[1] = 0.2f;
    material.emissiveFactor[2] = 0.3f;
    material.metallicFactor = 0.75f;
    material.roughnessFactor = 0.4f;
    material.textures[static_cast<size_t>(MaterialTextureSlot::normal)].scale = 1.0f;
    material.textures[static_cast<size_t>(MaterialTextureSlot::occlusion)].strength = 0.8f;
    material.alphaMode = MaterialAlphaMode::opaque;
    material.alphaCutoff = 0.5f;
    material.textures[static_cast<size_t>(MaterialTextureSlot::baseColor)]
        .texture.id = {11};
    material.textures[static_cast<size_t>(MaterialTextureSlot::normal)]
        .texture.id = {12};
    return material;
}

consteval bool material_constexpr_round_trip(void)
{
    constexpr Material material = make_material();
    uint8_t bytes[1101] = {};
    const ano::EncodeResult encoded = ano::encode(
        ano::ArtifactSource<Material>{&material, {nullptr, 0}},
        {bytes, sizeof(bytes)});
    if (encoded.error != ANO_RESOURCE_OK || encoded.size != sizeof(bytes))
        return false;
    const ano::DecodeResult<Material> decoded =
        ano::decode<Material>({bytes, sizeof(bytes)});
    if (decoded.error != ANO_RESOURCE_OK
        || decoded.view.value.baseColorFactor[1] != 0.5f
        || decoded.view.value.textures[
               static_cast<size_t>(MaterialTextureSlot::normal)]
               .texture.id.value != 12)
        return false;
    AnoResourceDependency dependencies[2] = {};
    const ano::DependencyResult extracted = ano::dependencies<Material>(
        {bytes, sizeof(bytes)}, dependencies, 2);
    return extracted.error == ANO_RESOURCE_OK && extracted.count == 2
        && dependencies[0].asset.value == 11
        && dependencies[1].asset.value == 12
        && dependencies[0].type.value == ano::resource_type_id<Texture>().value
        && dependencies[1].type.value == ano::resource_type_id<Texture>().value;
}

static_assert(material_constexpr_round_trip());

typedef struct MeshSourceExtent {
    Vertex vertices[3];
    uint32_t indices[3];
} MeshSourceExtent;

static MeshSourceExtent make_extent(void)
{
    return {
        .vertices = {
            {{-1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}},
            {{1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}},
            {{0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.5f, 1.0f}},
        },
        .indices = {0, 1, 2},
    };
}

static Mesh make_mesh(void)
{
    return {
        .vertices = {offsetof(MeshSourceExtent, vertices), 3},
        .indices = {offsetof(MeshSourceExtent, indices), 3},
        .material = {{UINT64_C(0x1122334455667788)}},
        .boundsMinimum = {-1.0f, 0.0f, 0.0f},
        .boundsMaximum = {1.0f, 1.0f, 0.0f},
    };
}

static void test_mesh_canonical_artifact(void)
{
    MeshSourceExtent extent = make_extent();
    Mesh mesh = make_mesh();
    const AnoResourceBytes sourceBytes = {
        reinterpret_cast<const uint8_t *>(&extent), sizeof(extent)};
    const ano::ArtifactSource<Mesh> source = {&mesh, sourceBytes};

    const ano::EncodeResult measured = ano::encoded_size(source);
    CHECK(measured.error == ANO_RESOURCE_OK && measured.size != 0,
          "mesh canonical size is measurable");

    CHECK(ano::encode(source, {nullptr, 0}).error
              == ANO_RESOURCE_BUFFER_TOO_SMALL,
          "mesh encoder reports required output capacity");

    Mesh invalidSource = mesh;
    invalidSource.vertices.offset = sizeof(extent);
    CHECK(ano::encoded_size(ano::ArtifactSource<Mesh>{&invalidSource, sourceBytes}).error
              == ANO_RESOURCE_OUT_OF_BOUNDS,
          "typed source extents are bounds checked");
    invalidSource = mesh;
    invalidSource.vertices.count = UINT64_MAX;
    CHECK(ano::encoded_size(ano::ArtifactSource<Mesh>{&invalidSource, sourceBytes}).error
              == ANO_RESOURCE_OVERFLOW,
          "typed source extent multiplication is checked");
    invalidSource = mesh;
    invalidSource.vertices.offset = 0;
    const AnoResourceBytes misaligned = {sourceBytes.data + 1,
                                         sourceBytes.size - 1};
    CHECK(ano::encoded_size(ano::ArtifactSource<Mesh>{&invalidSource, misaligned}).error
              == ANO_RESOURCE_MISALIGNED_SOURCE,
          "typed source objects require their declared alignment");

    uint8_t encoded[256] = {};
    const ano::EncodeResult encodedResult =
        ano::encode(source, {encoded, sizeof(encoded)});
    CHECK(encodedResult.error == ANO_RESOURCE_OK
          && encodedResult.size == measured.size,
          "mesh encodes into canonical bytes");
    if (encodedResult.error != ANO_RESOURCE_OK)
        return;

    const AnoResourceBytes artifact = {encoded, encodedResult.size};
    CHECK(ano::validate<Mesh>(artifact) == ANO_RESOURCE_OK,
          "canonical mesh validates");
    const ano::DecodeResult<Mesh> decoded = ano::decode<Mesh>(artifact);
    CHECK(decoded.error == ANO_RESOURCE_OK, "canonical mesh decodes");
    CHECK(decoded.view.value.vertices.count == 3
          && decoded.view.value.indices.count == 3,
          "decoded mesh exposes both canonical spans");
    CHECK(decoded.view.value.material.id.value == mesh.material.id.value,
          "decoded mesh preserves its stable material reference");

    Vertex middle = {};
    CHECK(ano::resolve(decoded.view, decoded.view.value.vertices, 1, &middle)
              == ANO_RESOURCE_OK,
          "typed view resolves a canonical vertex");
    CHECK(middle.position[0] == 1.0f && middle.texCoord[0] == 1.0f,
          "resolved vertex values match the source");

    ano::DependencyResult missing = ano::dependencies<Mesh>(artifact, nullptr, 0);
    CHECK(missing.error == ANO_RESOURCE_DEPENDENCY_CAPACITY && missing.count == 1,
          "dependency extraction reports required capacity");
    AnoResourceDependency dependency = {};
    const ano::DependencyResult extracted =
        ano::dependencies<Mesh>(artifact, &dependency, 1);
    CHECK(extracted.error == ANO_RESOURCE_OK && extracted.count == 1
          && dependency.asset.value == mesh.material.id.value
          && dependency.type.value == ano::resource_type_id<Material>().value,
          "dependency extraction emits the typed material reference");

    uint8_t repeated[256];
    memset(repeated, 0xa5, sizeof(repeated));
    const ano::EncodeResult repeatedResult =
        ano::encode(source, {repeated, sizeof(repeated)});
    CHECK(repeatedResult.error == ANO_RESOURCE_OK
          && memcmp(encoded, repeated, encodedResult.size) == 0,
          "canonical encoding ignores destination history and native padding");

    AnoContentId firstContent = {};
    AnoContentId secondContent = {};
    CHECK(ano_resource_content_id(artifact, &firstContent) == ANO_RESOURCE_OK
          && ano_resource_content_id({repeated, repeatedResult.size}, &secondContent)
              == ANO_RESOURCE_OK
          && memcmp(firstContent.bytes, secondContent.bytes,
                    sizeof(firstContent.bytes)) == 0,
          "identical canonical artifacts have identical content identities");

    uint8_t hostile[256];
    memcpy(hostile, encoded, encodedResult.size);
    hostile[0] ^= 1;
    CHECK(ano::validate<Mesh>({hostile, encodedResult.size})
              == ANO_RESOURCE_BAD_MAGIC,
          "bad artifact magic is rejected");

    memcpy(hostile, encoded, encodedResult.size);
    hostile[8] ^= 1;
    CHECK(ano::validate<Mesh>({hostile, encodedResult.size})
              == ANO_RESOURCE_TYPE_MISMATCH,
          "wrong semantic type is rejected");

    memcpy(hostile, encoded, encodedResult.size);
    hostile[16] ^= 1;
    CHECK(ano::validate<Mesh>({hostile, encodedResult.size})
              == ANO_RESOURCE_SCHEMA_MISMATCH,
          "wrong schema fingerprint is rejected");

    memcpy(hostile, encoded, encodedResult.size);
    hostile[64] = 129;
    CHECK(ano::validate<Mesh>({hostile, encodedResult.size})
              == ANO_RESOURCE_NON_CANONICAL,
          "non-canonical relative extent is rejected");

    CHECK(ano::validate<Mesh>({encoded, encodedResult.size - 1})
              == ANO_RESOURCE_NON_CANONICAL,
          "truncated payload is rejected before exposure");

    bool rejectedEveryTruncation = true;
    for (uint64_t size = 0; size < encodedResult.size; ++size)
        rejectedEveryTruncation = rejectedEveryTruncation
            && ano::validate<Mesh>({encoded, size}) != ANO_RESOURCE_OK;
    CHECK(rejectedEveryTruncation,
          "every truncated canonical mesh is rejected");
}

static void test_material_canonical_artifact(void)
{
    constexpr Material material = make_material();
    uint8_t encoded[2048] = {};
    const ano::EncodeResult result = ano::encode(
        ano::ArtifactSource<Material>{&material, {nullptr, 0}},
        {encoded, sizeof(encoded)});
    CHECK(result.error == ANO_RESOURCE_OK && result.size != 0,
          "material has a canonical representation");
    const ano::DecodeResult<Material> decoded =
        ano::decode<Material>({encoded, result.size});
    CHECK(decoded.error == ANO_RESOURCE_OK
          && decoded.view.value.metallicFactor == material.metallicFactor
          && decoded.view.value.textures[
               static_cast<size_t>(MaterialTextureSlot::baseColor)]
               .texture.id.value == 11,
          "material canonical values decode directly");
    AnoResourceDependency dependencies[2] = {};
    const ano::DependencyResult extracted = ano::dependencies<Material>(
        {encoded, result.size}, dependencies, 2);
    CHECK(extracted.error == ANO_RESOURCE_OK && extracted.count == 2
          && dependencies[0].asset.value == 11
          && dependencies[1].asset.value == 12
          && dependencies[0].type.value == ano::resource_type_id<Texture>().value
          && dependencies[1].type.value == ano::resource_type_id<Texture>().value,
          "material texture dependencies follow reflected field order");

    Material invalid = material;
    invalid.alphaMode = static_cast<MaterialAlphaMode>(UINT8_MAX);
    CHECK(ano::encode(ano::ArtifactSource<Material>{&invalid, {nullptr, 0}},
                      {encoded, sizeof(encoded)}).error
              == ANO_RESOURCE_NON_CANONICAL,
          "encoder rejects values outside a reflected enum");

    encoded[104] = UINT8_MAX;
    CHECK(ano::validate<Material>({encoded, result.size})
              == ANO_RESOURCE_NON_CANONICAL,
          "decoder rejects values outside a reflected enum");
}

static void test_generated_artifact_dispatch(void)
{
    constexpr Material material = make_material();
    uint8_t encoded[2048] = {};
    const ano::EncodeResult result = ano::encode(
        ano::ArtifactSource<Material>{&material, {nullptr, 0}},
        {encoded, sizeof(encoded)});
    CHECK(result.error == ANO_RESOURCE_OK,
          "dispatch fixture encodes");

    constexpr AnoResourceTypeId materialType = ano::resource_type_id<Material>();
    AnoResourceSchema schema = {};
    CHECK(ano_resource_artifact_schema(materialType, &schema) == ANO_RESOURCE_OK
          && schema.type.value == materialType.value
          && schema.fixedSize == ano::fixed_wire_size<Material>()
          && fingerprint_equal(schema.fingerprint,
                               ano::schema_fingerprint<Material>()),
          "reflected universe generates schema dispatch");
    CHECK(ano_resource_artifact_schema(materialType, nullptr)
              == ANO_RESOURCE_INVALID_ARGUMENT,
          "schema dispatch rejects a null output");
    CHECK(ano_resource_artifact_schema({UINT64_MAX}, &schema)
              == ANO_RESOURCE_TYPE_MISMATCH,
          "schema dispatch rejects an unknown reflected type");

    const AnoResourceBytes bytes = {encoded, result.size};
    CHECK(ano_resource_validate_artifact(materialType, bytes)
              == ANO_RESOURCE_OK,
          "reflected universe generates validation dispatch");
    CHECK(ano_resource_validate_artifact({UINT64_MAX}, bytes)
              == ANO_RESOURCE_TYPE_MISMATCH,
          "validation dispatch rejects an unknown reflected type");

    AnoResourceDependency dependencies[2] = {};
    uint64_t dependencyCount = 0;
    CHECK(ano_resource_artifact_dependencies(
              materialType, bytes, dependencies, 2, &dependencyCount)
              == ANO_RESOURCE_OK
          && dependencyCount == 2
          && dependencies[0].asset.value == 11
          && dependencies[1].asset.value == 12
          && dependencies[0].type.value == ano::resource_type_id<Texture>().value
          && dependencies[1].type.value == ano::resource_type_id<Texture>().value,
          "reflected universe generates dependency dispatch");
    CHECK(ano_resource_artifact_dependencies(
              materialType, bytes, nullptr, 0, &dependencyCount)
              == ANO_RESOURCE_DEPENDENCY_CAPACITY
          && dependencyCount == 2,
          "dependency dispatch reports required capacity");
    CHECK(ano_resource_artifact_dependencies(
              materialType, bytes, dependencies, 2, nullptr)
              == ANO_RESOURCE_INVALID_ARGUMENT,
          "dependency dispatch rejects a null count output");
}

static void test_sha256(void)
{
    static constexpr uint8_t expected[32] = {
        0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea,
        0x41, 0x41, 0x40, 0xde, 0x5d, 0xae, 0x22, 0x23,
        0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c,
        0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad,
    };
    AnoContentId content = {};
    CHECK(ano_resource_content_id({abcBytes, sizeof(abcBytes)}, &content)
              == ANO_RESOURCE_OK
          && memcmp(content.bytes, expected, sizeof(expected)) == 0,
          "content identity matches the published SHA-256 abc vector");
    bool runtimeMatchesConstexpr = true;
    for (uint32_t i = 0; i < 10; ++i) {
        runtimeMatchesConstexpr = runtimeMatchesConstexpr
            && ano_resource_content_id(
                   {shaPattern.bytes, shaLengths[i]}, &content)
                == ANO_RESOURCE_OK
            && memcmp(content.bytes, shaCases.values[i].bytes,
                      sizeof(content.bytes)) == 0;
    }
    CHECK(runtimeMatchesConstexpr,
          "runtime SHA-256 matches constexpr across block boundaries");
    CHECK(strcmp(ano_resource_error_string(ANO_RESOURCE_SCHEMA_MISMATCH),
                 "schema_mismatch") == 0,
          "resource error names come from the reflected enum");
}

int main(void)
{
    test_mesh_canonical_artifact();
    test_material_canonical_artifact();
    test_generated_artifact_dispatch();
    test_sha256();
    if (failures == 0) {
        printf("anotest_resources_language: all checks passed\n");
        return 0;
    }
    printf("anotest_resources_language: %d check(s) failed\n", failures);
    return 1;
}
