/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0
 * Anoptic targets ISO C++26. */
/*  == Anoptic Game Engine v0.0000001 == */

#include <anoptic_render_resources.h>

using namespace ano;
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

static ResourceResult<AnoResourceMutableBytes> cook_artifacts(
    const TestArtifact *items, uint64_t count)
{
    const auto created = resource_cooker_create(
        {.firstDerivedAsset = {count + 2}});
    if (!created)
        return failure(created.error());
    AnoResourceCooker *cooker = *created;
    ResourceResult<> result{};
    for (uint64_t i = 0; i < count && result; ++i)
        result = resource_cooker_add(
            cooker, items[i].asset, items[i].type, items[i].commitGroup,
            items[i].bytes);
    const AnoCookedRevision *revision = nullptr;
    if (result) {
        const auto cooked = resource_cook(cooker);
        if (cooked)
            revision = *cooked;
        else
            result = failure(cooked.error());
    }
    auto pack = result ? resource_revision_export_pack(revision)
                       : ResourceResult<AnoResourceMutableBytes>(
                             failure(result.error()));
    resource_revision_release(revision);
    resource_cooker_destroy(cooker);
    return pack;
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
    CHECK(texture && material,
          "pack fixtures encode as canonical artifacts");
    if (!texture || !material)
        return;

    constexpr AnoResourceTypeId textureType = ano::resource_type_id<Texture>();
    constexpr AnoResourceTypeId materialType = ano::resource_type_id<Material>();
    const TestArtifact scrambled[3] = {
        {{3}, materialType, {2}, {materialBytes, *material}},
        {{1}, textureType, {1}, {textureBytes, *texture}},
        {{2}, textureType, {1}, {textureBytes, *texture}},
    };
    const auto firstResult = cook_artifacts(scrambled, 3);
    CHECK(firstResult && firstResult->size > *material + *texture,
          "pack construction succeeds");
    if (!firstResult)
        return;
    AnoResourceMutableBytes first = *firstResult;

    const TestArtifact ordered[3] = {
        {{1}, textureType, {1}, {textureBytes, *texture}},
        {{2}, textureType, {1}, {textureBytes, *texture}},
        {{3}, materialType, {2}, {materialBytes, *material}},
    };
    const auto secondResult = cook_artifacts(ordered, 3);
    AnoResourceMutableBytes second = secondResult.value_or(
        AnoResourceMutableBytes{});
    CHECK(second.size == first.size
          && memcmp(first.data, second.data, static_cast<size_t>(first.size))
              == 0,
          "pack bytes ignore input order and destination history");

    auto openedResult = resource_pack_open({first.data, first.size});
    AnoResourcePack *opened = openedResult.value_or(nullptr);
    CHECK(opened != nullptr,
          "authenticated pack opens");
    if (opened == nullptr) {
        resource_exported_pack_release(second);
        resource_exported_pack_release(first);
        return;
    }
    const AnoResourceManifest *manifest = resource_pack_manifest(opened);
    CHECK(resource_manifest_entry_count(manifest) == 3,
          "pack exposes its completely validated manifest");

    const auto manifestId = resource_manifest_id(manifest);
    CHECK(manifestId.has_value(),
          "manifest has a content identity");
    bool nonzeroId = false;
    for (uint8_t byte : manifestId->bytes)
        nonzeroId = nonzeroId || byte != 0;
    CHECK(nonzeroId, "manifest identity is nonzero");

    const auto firstTexture = resource_manifest_find(manifest, {1});
    const auto secondTexture = resource_manifest_find(manifest, {2});
    const auto materialEntry = resource_manifest_find(manifest, {3});
    CHECK(firstTexture && secondTexture && materialEntry,
          "dense asset IDs resolve directly");
    const auto firstView = resource_pack_view(opened, {1});
    const auto secondView = resource_pack_view(opened, {2});
    CHECK(firstView && secondView && firstView->size == secondView->size,
          "identical artifacts expose equal public ranges");
    CHECK(firstView->size == *texture
          && memcmp(firstView->data, secondView->data,
                    static_cast<size_t>(firstView->size)) == 0,
          "equal public ranges retain identical canonical bytes");
    CHECK(materialEntry->type.value == materialType.value
          && materialEntry->dependencyCount == 1,
          "material manifest entry retains its reflected type and dependency");

    const auto dependency = resource_manifest_dependency(manifest, {3}, 0);
    CHECK(dependency && dependency->asset.value == 1
          && dependency->type.value == textureType.value,
          "manifest retains the reflected AssetRef target type");
    CHECK(ano::has_error(resource_manifest_find(manifest, {4}),
                         ANO_RESOURCE_NOT_FOUND),
          "manifest rejects an absent stable ID");

    const auto openedRevisionResult = resource_pack_revision(opened);
    const AnoCookedRevision *openedRevision =
        openedRevisionResult.value_or(nullptr);
    auto revisionView = resource_revision_resolve(
        openedRevision, {3}, materialType);
    CHECK(revisionView && revisionView->size == *material
          && memcmp(revisionView->data, materialBytes,
                    static_cast<size_t>(*material)) == 0,
          "pack opening constructs the runtime cooked revision");
    resource_pack_close(opened);
    CHECK(resource_revision_resolve(
              openedRevision, {3}, materialType).has_value(),
          "retained pack revision owns its authenticated backing volume");
    resource_revision_release(openedRevision);

    first.data[64] ^= 1;
    CHECK(ano::has_error(resource_pack_open({first.data, first.size}),
                         ANO_RESOURCE_BAD_MANIFEST),
          "manifest authentication rejects modified bytes");
    first.data[64] ^= 1;

    const uint64_t payloadOffset = read_u64(first.data + 24);
    const uint64_t manifestSize = read_u64(first.data + 8);
    const uint64_t firstOffset = read_u64(first.data + 64 + manifestSize);
    first.data[payloadOffset + firstOffset] ^= 1;
    CHECK(ano::has_error(resource_pack_open({first.data, first.size}),
                         ANO_RESOURCE_BAD_PACK),
          "artifact authentication rejects modified payload bytes");
    first.data[payloadOffset + firstOffset] ^= 1;
    resource_exported_pack_release(second);
    resource_exported_pack_release(first);
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
    const TestArtifact wrongType = {
        {1}, materialType, {1}, {textureBytes, texture.value_or(0)},
    };
    CHECK(ano::has_error(cook_artifacts(&wrongType, 1),
                         ANO_RESOURCE_NON_CANONICAL),
          "builder rejects bytes presented as the wrong reflected type");

    const TestArtifact missingTypedTarget = {
        {1}, materialType, {1}, {materialBytes, material.value_or(0)},
    };
    CHECK(ano::has_error(cook_artifacts(&missingTypedTarget, 1),
                         ANO_RESOURCE_BAD_MANIFEST),
          "builder rejects a dependency bound to the wrong manifest type");

    const TestArtifact duplicate[2] = {
        {{1}, textureType, {1}, {textureBytes, texture.value_or(0)}},
        {{1}, textureType, {1}, {textureBytes, texture.value_or(0)}},
    };
    CHECK(ano::has_error(cook_artifacts(duplicate, 2),
                         ANO_RESOURCE_DUPLICATE_ASSET),
          "builder rejects duplicate stable asset IDs");
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
