/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0
 * Anoptic targets ISO C++26. */
/*  == Anoptic Game Engine v0.0000001 == */

#include <anoptic_render_resources.h>
#include <anoptic_resources_pack.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

using ano::asset_schema::Material;
using ano::asset_schema::MaterialTextureSlot;
using ano::asset_schema::Texture;
using ano::asset_schema::TextureFormat;
using ano::asset_schema::TextureUsage;

static int failures = 0;

#define CHECK(condition, message) do { \
    if (!(condition)) { \
        printf("FAIL: %s (%s:%d)\n", (message), __FILE__, __LINE__); \
        ++failures; \
    } \
} while (0)

struct TestArtifact final {
    AnoAssetId asset;
    AnoResourceTypeId type;
    AnoResourceCommitGroupId commitGroup;
    AnoResourceBytes bytes;
};

static AnoResourceError cook_artifacts(const TestArtifact *items,
                                       uint64_t count,
                                       AnoResourceMutableBytes *pack)
{
    AnoResourceCooker *cooker = nullptr;
    AnoResourceError result = ano_resource_cooker_create(
        {.firstDerivedAsset = {count + 2}}, &cooker);
    for (uint64_t i = 0; i < count && result == ANO_RESOURCE_OK; ++i)
        result = ano_resource_cooker_add(
            cooker, items[i].asset, items[i].type, items[i].commitGroup,
            items[i].bytes);
    const AnoCookedRevision *revision = nullptr;
    if (result == ANO_RESOURCE_OK)
        result = ano_resource_cook(cooker, &revision);
    if (result == ANO_RESOURCE_OK)
        result = ano_resource_revision_export_pack(revision, pack);
    ano_resource_revision_release(revision);
    ano_resource_cooker_destroy(cooker);
    return result;
}

static uint64_t read_u64(const uint8_t *bytes)
{
    uint64_t value = 0;
    for (uint32_t i = 0; i < 8; ++i)
        value |= static_cast<uint64_t>(bytes[i]) << (i * 8u);
    return value;
}

static ano::EncodeResult encode_texture(uint8_t *output, uint64_t capacity)
{
    static const uint8_t pixels[4] = {0x10, 0x20, 0x30, 0xff};
    const Texture texture = {
        .width = 1,
        .height = 1,
        .mipCount = 1,
        .format = TextureFormat::rgba8,
        .usage = TextureUsage::color,
        .bytes = {0, sizeof(pixels)},
    };
    return ano::encode(
        ano::ArtifactSource<Texture>{
            .value = &texture,
            .extent = {.data = pixels, .size = sizeof(pixels)},
        },
        {.data = output, .size = capacity});
}

static ano::EncodeResult encode_material(uint8_t *output, uint64_t capacity,
                                         AnoAssetId texture)
{
    Material material = {};
    material.baseColorFactor[0] = 1.0f;
    material.baseColorFactor[1] = 1.0f;
    material.baseColorFactor[2] = 1.0f;
    material.baseColorFactor[3] = 1.0f;
    material.metallicFactor = 1.0f;
    material.roughnessFactor = 1.0f;
    material.textures[static_cast<size_t>(MaterialTextureSlot::baseColor)]
        .texture.id = texture;
    return ano::encode(
        ano::ArtifactSource<Material>{
            .value = &material,
            .extent = {.data = nullptr, .size = 0},
        },
        {.data = output, .size = capacity});
}

static void test_pack_round_trip(void)
{
    uint8_t textureBytes[128] = {};
    uint8_t materialBytes[1200] = {};
    const ano::EncodeResult texture =
        encode_texture(textureBytes, sizeof(textureBytes));
    const ano::EncodeResult material =
        encode_material(materialBytes, sizeof(materialBytes), {1});
    CHECK(texture.error == ANO_RESOURCE_OK
          && material.error == ANO_RESOURCE_OK,
          "pack fixtures encode as canonical artifacts");
    if (texture.error != ANO_RESOURCE_OK || material.error != ANO_RESOURCE_OK)
        return;

    constexpr AnoResourceTypeId textureType = ano::resource_type_id<Texture>();
    constexpr AnoResourceTypeId materialType = ano::resource_type_id<Material>();
    const TestArtifact scrambled[3] = {
        {{3}, materialType, {2}, {materialBytes, material.size}},
        {{1}, textureType, {1}, {textureBytes, texture.size}},
        {{2}, textureType, {1}, {textureBytes, texture.size}},
    };
    AnoResourceMutableBytes first = {};
    CHECK(cook_artifacts(scrambled, 3, &first) == ANO_RESOURCE_OK
          && first.size > material.size + texture.size,
          "pack construction succeeds");
    if (first.data == nullptr)
        return;

    const TestArtifact ordered[3] = {
        {{1}, textureType, {1}, {textureBytes, texture.size}},
        {{2}, textureType, {1}, {textureBytes, texture.size}},
        {{3}, materialType, {2}, {materialBytes, material.size}},
    };
    AnoResourceMutableBytes second = {};
    CHECK(cook_artifacts(ordered, 3, &second) == ANO_RESOURCE_OK
          && second.size == first.size
          && memcmp(first.data, second.data, static_cast<size_t>(first.size))
              == 0,
          "pack bytes ignore input order and destination history");

    AnoResourcePack *opened = nullptr;
    CHECK(ano_resource_pack_open({first.data, first.size}, &opened)
              == ANO_RESOURCE_OK
          && opened != nullptr,
          "authenticated pack opens");
    if (opened == nullptr) {
        ano_resource_exported_pack_release(second);
        ano_resource_exported_pack_release(first);
        return;
    }
    const AnoResourceManifest *manifest = ano_resource_pack_manifest(opened);
    CHECK(ano_resource_manifest_entry_count(manifest) == 3,
          "pack exposes its completely validated manifest");

    AnoManifestId manifestId = {};
    CHECK(ano_resource_manifest_id(manifest, &manifestId) == ANO_RESOURCE_OK,
          "manifest has a content identity");
    bool nonzeroId = false;
    for (uint8_t byte : manifestId.bytes)
        nonzeroId = nonzeroId || byte != 0;
    CHECK(nonzeroId, "manifest identity is nonzero");

    AnoResourceManifestEntry firstTexture = {};
    AnoResourceManifestEntry secondTexture = {};
    AnoResourceManifestEntry materialEntry = {};
    CHECK(ano_resource_manifest_find(manifest, {1}, &firstTexture)
              == ANO_RESOURCE_OK
          && ano_resource_manifest_find(manifest, {2}, &secondTexture)
              == ANO_RESOURCE_OK
          && ano_resource_manifest_find(manifest, {3}, &materialEntry)
              == ANO_RESOURCE_OK,
          "dense asset IDs resolve directly");
    AnoResourceBytes firstView = {};
    AnoResourceBytes secondView = {};
    CHECK(ano_resource_pack_view(opened, {1}, &firstView) == ANO_RESOURCE_OK
          && ano_resource_pack_view(opened, {2}, &secondView)
              == ANO_RESOURCE_OK
          && firstView.size == secondView.size,
          "identical artifacts expose equal public ranges");
    CHECK(firstView.size == texture.size
          && memcmp(firstView.data, secondView.data,
                    static_cast<size_t>(firstView.size)) == 0,
          "equal public ranges retain identical canonical bytes");
    CHECK(materialEntry.type.value == materialType.value
          && materialEntry.dependencyCount == 1,
          "material manifest entry retains its reflected type and dependency");

    AnoResourceDependency dependency = {};
    CHECK(ano_resource_manifest_dependency(manifest, {3}, 0, &dependency)
              == ANO_RESOURCE_OK
          && dependency.asset.value == 1
          && dependency.type.value == textureType.value,
          "manifest retains the reflected AssetRef target type");
    CHECK(ano_resource_manifest_find(manifest, {4}, &materialEntry)
              == ANO_RESOURCE_NOT_FOUND,
          "manifest rejects an absent stable ID");

    uint64_t readSize = 0;
    CHECK(ano_resource_pack_read(opened, {3}, {nullptr, 0}, &readSize)
              == ANO_RESOURCE_BUFFER_TOO_SMALL
          && readSize == material.size,
          "range read reports the exact unpacked size");
    uint8_t readback[1200] = {};
    CHECK(ano_resource_pack_read(opened, {3},
                                 {readback, sizeof(readback)}, &readSize)
              == ANO_RESOURCE_OK
          && readSize == material.size
          && memcmp(readback, materialBytes,
                    static_cast<size_t>(material.size)) == 0,
          "range read authenticates and copies canonical artifact bytes");
    const AnoCookedRevision *openedRevision = nullptr;
    AnoResourceBytes revisionView{};
    CHECK(ano_resource_pack_revision(opened, &openedRevision)
              == ANO_RESOURCE_OK
          && ano_resource_revision_resolve(
                 openedRevision, {3}, materialType, &revisionView)
              == ANO_RESOURCE_OK
          && revisionView.size == material.size
          && memcmp(revisionView.data, materialBytes,
                    static_cast<size_t>(material.size)) == 0,
          "pack opening constructs the runtime cooked revision");
    ano_resource_pack_close(opened);
    CHECK(ano_resource_revision_resolve(
              openedRevision, {3}, materialType, &revisionView)
              == ANO_RESOURCE_OK,
          "retained pack revision owns its authenticated backing volume");
    ano_resource_revision_release(openedRevision);

    first.data[64] ^= 1;
    CHECK(ano_resource_pack_open({first.data, first.size}, &opened)
              == ANO_RESOURCE_BAD_MANIFEST,
          "manifest authentication rejects modified bytes");
    first.data[64] ^= 1;

    const uint64_t payloadOffset = read_u64(first.data + 24);
    const uint64_t manifestSize = read_u64(first.data + 8);
    const uint64_t firstOffset = read_u64(first.data + 64 + manifestSize);
    first.data[payloadOffset + firstOffset] ^= 1;
    CHECK(ano_resource_pack_open({first.data, first.size}, &opened)
              == ANO_RESOURCE_BAD_PACK,
          "artifact authentication rejects modified payload bytes");
    first.data[payloadOffset + firstOffset] ^= 1;
    ano_resource_exported_pack_release(second);
    ano_resource_exported_pack_release(first);
}

static void test_pack_rejects_bad_closure(void)
{
    uint8_t textureBytes[128] = {};
    uint8_t materialBytes[1200] = {};
    const ano::EncodeResult texture =
        encode_texture(textureBytes, sizeof(textureBytes));
    const ano::EncodeResult material =
        encode_material(materialBytes, sizeof(materialBytes), {1});
    constexpr AnoResourceTypeId textureType = ano::resource_type_id<Texture>();
    constexpr AnoResourceTypeId materialType = ano::resource_type_id<Material>();
    AnoResourceMutableBytes pack = {};
    const TestArtifact wrongType = {
        {1}, materialType, {1}, {textureBytes, texture.size},
    };
    CHECK(cook_artifacts(&wrongType, 1, &pack)
              == ANO_RESOURCE_NON_CANONICAL,
          "builder rejects bytes presented as the wrong reflected type");

    const TestArtifact missingTypedTarget = {
        {1}, materialType, {1}, {materialBytes, material.size},
    };
    CHECK(cook_artifacts(&missingTypedTarget, 1, &pack)
              == ANO_RESOURCE_BAD_MANIFEST,
          "builder rejects a dependency bound to the wrong manifest type");

    const TestArtifact duplicate[2] = {
        {{1}, textureType, {1}, {textureBytes, texture.size}},
        {{1}, textureType, {1}, {textureBytes, texture.size}},
    };
    CHECK(cook_artifacts(duplicate, 2, &pack)
              == ANO_RESOURCE_DUPLICATE_ASSET,
          "builder rejects duplicate stable asset IDs");
    ano_resource_exported_pack_release(pack);
}

int main(void)
{
    test_pack_round_trip();
    test_pack_rejects_bad_closure();
    if (failures == 0) {
        printf("anotest_resources_pack: all checks passed\n");
        return 0;
    }
    printf("anotest_resources_pack: %d check(s) failed\n", failures);
    return 1;
}
