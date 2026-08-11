/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0
 * Anoptic targets ISO C++26. Compiler/library incompleteness disqualifies the toolchain; it does not constrain the architecture. */
/*  == Anoptic Game Engine v0.0000001 == */

#include <anoptic_render_resources.h>
#include <anoptic_resources_runtime.h>

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

struct Fixture final {
    uint8_t texture[128];
    uint64_t textureSize;
    uint8_t material[1200];
    uint64_t materialSize;
    AnoResourceMutableBytes pack;
};

static bool make_fixture(uint8_t red, Fixture *fixture)
{
    if (fixture == nullptr)
        return false;
    *fixture = {};
    const uint8_t pixels[4] = {red, 0x20, 0x30, 0xff};
    const Texture texture = {
        .width = 1,
        .height = 1,
        .mipCount = 1,
        .format = TextureFormat::rgba8,
        .usage = TextureUsage::color,
        .bytes = {0, sizeof(pixels)},
    };
    const ano::EncodeResult encodedTexture = ano::encode(
        ano::ArtifactSource<Texture>{
            .value = &texture,
            .extent = {.data = pixels, .size = sizeof(pixels)},
        },
        {.data = fixture->texture, .size = sizeof(fixture->texture)});
    if (encodedTexture.error != ANO_RESOURCE_OK)
        return false;
    fixture->textureSize = encodedTexture.size;

    Material material = {};
    material.baseColorFactor[0] = 1.0f;
    material.baseColorFactor[1] = 1.0f;
    material.baseColorFactor[2] = 1.0f;
    material.baseColorFactor[3] = 1.0f;
    material.roughnessFactor = 1.0f;
    material.textures[static_cast<size_t>(MaterialTextureSlot::baseColor)]
        .texture.id = {1};
    const ano::EncodeResult encodedMaterial = ano::encode(
        ano::ArtifactSource<Material>{
            .value = &material,
            .extent = {.data = nullptr, .size = 0},
        },
        {.data = fixture->material, .size = sizeof(fixture->material)});
    if (encodedMaterial.error != ANO_RESOURCE_OK)
        return false;
    fixture->materialSize = encodedMaterial.size;

    constexpr AnoResourceTypeId textureType = ano::resource_type_id<Texture>();
    constexpr AnoResourceTypeId materialType = ano::resource_type_id<Material>();
    AnoResourceCooker *cooker = nullptr;
    AnoResourceError result = ano_resource_cooker_create(
        {.firstDerivedAsset = {3}}, &cooker);
    if (result == ANO_RESOURCE_OK)
        result = ano_resource_cooker_add(
            cooker, {2}, materialType, {7},
            {fixture->material, fixture->materialSize});
    if (result == ANO_RESOURCE_OK)
        result = ano_resource_cooker_add(
            cooker, {1}, textureType, {7},
            {fixture->texture, fixture->textureSize});
    if (result == ANO_RESOURCE_OK)
        result = ano_resource_cook(cooker, &fixture->pack);
    ano_resource_cooker_destroy(cooker);
    return result == ANO_RESOURCE_OK;
}

static void check_changed(const AnoResidencyEpoch *epoch,
                          const uint64_t *expected, uint64_t count,
                          const char *message)
{
    bool equal = ano_resource_epoch_changed_count(epoch) == count;
    for (uint64_t i = 0; i < count && equal; ++i) {
        AnoAssetId asset = {};
        equal = ano_resource_epoch_changed(epoch, i, &asset) == ANO_RESOURCE_OK
            && asset.value == expected[i];
    }
    CHECK(equal, message);
}

static void test_residency_epochs(void)
{
    Fixture original = {};
    Fixture replacement = {};
    const bool fixtures = make_fixture(0x10, &original)
        && make_fixture(0x90, &replacement);
    CHECK(fixtures,
          "runtime fixtures build as complete packs");
    if (!fixtures) {
        ano_resource_cooked_pack_release(replacement.pack);
        ano_resource_cooked_pack_release(original.pack);
        return;
    }

    AnoResourceManager *manager = nullptr;
    CHECK(ano_resource_manager_create(
              {original.pack.data, original.pack.size}, &manager)
              == ANO_RESOURCE_OK
          && manager != nullptr,
          "manager opens the initial pack");
    if (manager == nullptr) {
        ano_resource_cooked_pack_release(replacement.pack);
        ano_resource_cooked_pack_release(original.pack);
        return;
    }

    constexpr AnoResourceTypeId textureType = ano::resource_type_id<Texture>();
    constexpr AnoResourceTypeId materialType = ano::resource_type_id<Material>();
    const AnoResidencyEpoch *empty = nullptr;
    CHECK(ano_resource_epoch_acquire(manager, &empty) == ANO_RESOURCE_OK
          && ano_resource_epoch_id(empty).value == 1,
          "manager begins with an immutable empty epoch");
    AnoResourceBytes resolved = {};
    CHECK(ano_resource_epoch_resolve(empty, {1}, textureType, &resolved)
              == ANO_RESOURCE_NOT_FOUND,
          "empty epoch exposes no undemanded artifact");

    AnoResourceGoal wrong = {
        .goal = {1},
        .asset = {2},
        .type = textureType,
        .commitGroup = {7},
        .quality = {ANO_RESOURCE_QUALITY_WHOLE},
        .importance = 1.0f,
    };
    CHECK(ano_resource_goal_set(manager, wrong)
              == ANO_RESOURCE_TYPE_MISMATCH,
          "goal type must match the reflected manifest type");

    const AnoResourceGoal materialGoal = {
        .goal = {1},
        .asset = {2},
        .type = materialType,
        .commitGroup = {7},
        .quality = {ANO_RESOURCE_QUALITY_WHOLE},
        .importance = 1.0f,
    };
    CHECK(ano_resource_goal_set(manager, materialGoal) == ANO_RESOURCE_OK
          && ano_resource_reconcile(manager) == ANO_RESOURCE_OK,
          "reconciliation materializes the complete commit-group floor");

    const AnoResidencyEpoch *resident = nullptr;
    CHECK(ano_resource_epoch_acquire(manager, &resident) == ANO_RESOURCE_OK
          && ano_resource_epoch_id(resident).value == 2,
          "complete demand publishes one successor epoch");
    const uint64_t firstChanged[2] = {1, 2};
    check_changed(resident, firstChanged, 2,
                  "first publication reports both new bindings");
    CHECK(ano_resource_epoch_resolve(resident, {1}, textureType, &resolved)
              == ANO_RESOURCE_OK
          && resolved.size == original.textureSize
          && memcmp(resolved.data, original.texture,
                    static_cast<size_t>(resolved.size)) == 0,
          "dependency artifact resolves from the resident epoch");
    CHECK(ano_resource_epoch_resolve(resident, {2}, materialType, &resolved)
              == ANO_RESOURCE_OK
          && resolved.size == original.materialSize,
          "root artifact resolves from the same epoch");
    CHECK(ano_resource_epoch_resolve(empty, {1}, textureType, &resolved)
              == ANO_RESOURCE_NOT_FOUND,
          "retained previous epoch does not observe new bindings");

    CHECK(ano_resource_reconcile(manager) == ANO_RESOURCE_OK,
          "unchanged reconciliation succeeds");
    const AnoResidencyEpoch *unchanged = nullptr;
    CHECK(ano_resource_epoch_acquire(manager, &unchanged) == ANO_RESOURCE_OK
          && ano_resource_epoch_id(unchanged).value == 2,
          "unchanged reconciliation publishes no redundant epoch");
    ano_resource_epoch_release(unchanged);

    replacement.pack.data[replacement.pack.size - 1] ^= 1;
    AnoResourceReload *prepared = nullptr;
    CHECK(ano_resource_reload_prepare(
              manager, {replacement.pack.data, replacement.pack.size},
              &prepared) == ANO_RESOURCE_BAD_PACK
          && prepared == nullptr,
          "corrupt candidate reload is rejected");
    replacement.pack.data[replacement.pack.size - 1] ^= 1;
    const AnoResidencyEpoch *afterFailure = nullptr;
    CHECK(ano_resource_epoch_acquire(manager, &afterFailure) == ANO_RESOURCE_OK
          && ano_resource_epoch_id(afterFailure).value == 2
          && ano_resource_epoch_resolve(afterFailure, {1}, textureType,
                                        &resolved) == ANO_RESOURCE_OK
          && memcmp(resolved.data, original.texture,
                    static_cast<size_t>(resolved.size)) == 0,
          "failed reload preserves the published generation");
    ano_resource_epoch_release(afterFailure);

    CHECK(ano_resource_reload_prepare(
              manager, {replacement.pack.data, replacement.pack.size},
              &prepared)
              == ANO_RESOURCE_OK
          && prepared != nullptr
          && ano_resource_reload_has_changes(prepared),
          "replacement prepares as a private changed generation");
    const AnoResidencyEpoch *candidate = ano_resource_reload_epoch(prepared);
    CHECK(candidate != nullptr
          && ano_resource_epoch_resolve(candidate, {1}, textureType, &resolved)
              == ANO_RESOURCE_OK
          && memcmp(resolved.data, replacement.texture,
                    static_cast<size_t>(resolved.size)) == 0,
          "owner preparation can resolve replacement bytes before publication");
    const AnoResidencyEpoch *duringPrepare = nullptr;
    CHECK(ano_resource_epoch_acquire(manager, &duringPrepare) == ANO_RESOURCE_OK
          && ano_resource_epoch_id(duringPrepare).value == 2
          && ano_resource_epoch_resolve(duringPrepare, {1}, textureType,
                                        &resolved) == ANO_RESOURCE_OK
          && memcmp(resolved.data, original.texture,
                    static_cast<size_t>(resolved.size)) == 0,
          "readers retain the published generation while owners prepare");
    ano_resource_epoch_release(duringPrepare);
    ano_resource_reload_abort(prepared);
    prepared = nullptr;
    CHECK(ano_resource_epoch_acquire(manager, &duringPrepare) == ANO_RESOURCE_OK
          && ano_resource_epoch_id(duringPrepare).value == 2,
          "aborting owner preparation preserves the published epoch");
    ano_resource_epoch_release(duringPrepare);

    CHECK(ano_resource_reload_prepare(
              manager, {replacement.pack.data, replacement.pack.size},
              &prepared)
              == ANO_RESOURCE_OK
          && ano_resource_reload_commit(prepared) == ANO_RESOURCE_OK,
          "owner-approved replacement publishes transactionally");
    const AnoResidencyEpoch *reloaded = nullptr;
    CHECK(ano_resource_epoch_acquire(manager, &reloaded) == ANO_RESOURCE_OK
          && ano_resource_epoch_id(reloaded).value == 3,
          "successful reload advances the epoch once");
    const uint64_t reloadChanged[1] = {1};
    check_changed(reloaded, reloadChanged, 1,
                  "reload identifies only changed artifact content");
    CHECK(ano_resource_epoch_resolve(reloaded, {1}, textureType, &resolved)
              == ANO_RESOURCE_OK
          && memcmp(resolved.data, replacement.texture,
                    static_cast<size_t>(resolved.size)) == 0,
          "successor epoch exposes replacement content");
    CHECK(ano_resource_epoch_resolve(resident, {1}, textureType, &resolved)
              == ANO_RESOURCE_OK
          && memcmp(resolved.data, original.texture,
                    static_cast<size_t>(resolved.size)) == 0,
          "retained reader keeps the complete previous generation");

    CHECK(ano_resource_goal_remove(manager, {1}) == ANO_RESOURCE_OK
          && ano_resource_goal_remove(manager, {1})
              == ANO_RESOURCE_NOT_FOUND
          && ano_resource_reconcile(manager) == ANO_RESOURCE_OK,
          "removing demand reconciles rather than unloading directly");
    const AnoResidencyEpoch *retired = nullptr;
    CHECK(ano_resource_epoch_acquire(manager, &retired) == ANO_RESOURCE_OK
          && ano_resource_epoch_id(retired).value == 4
          && ano_resource_epoch_resolve(retired, {1}, textureType, &resolved)
              == ANO_RESOURCE_NOT_FOUND,
          "removed demand publishes a new empty binding set");
    const uint64_t retiredChanged[2] = {1, 2};
    check_changed(retired, retiredChanged, 2,
                  "retirement reports both removed bindings");
    ano_resource_epoch_release(retired);

    ano_resource_manager_destroy(manager);
    CHECK(ano_resource_epoch_resolve(reloaded, {1}, textureType, &resolved)
              == ANO_RESOURCE_OK
          && memcmp(resolved.data, replacement.texture,
                    static_cast<size_t>(resolved.size)) == 0,
          "acquired epoch outlives its manager and later generations");

    ano_resource_epoch_release(reloaded);
    ano_resource_epoch_release(resident);
    ano_resource_epoch_release(empty);
    ano_resource_cooked_pack_release(replacement.pack);
    ano_resource_cooked_pack_release(original.pack);
}

int main(void)
{
    test_residency_epochs();
    if (failures == 0) {
        printf("anotest_resources_runtime: all checks passed\n");
        return 0;
    }
    printf("anotest_resources_runtime: %d check(s) failed\n", failures);
    return 1;
}
