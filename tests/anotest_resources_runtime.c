/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0
 * Anoptic targets ISO C++26. */
/*  == Anoptic Game Engine v0.0000001 == */

#include <anoptic_render_resources.h>

using namespace ano;
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
    const AnoCookedRevision *revision;
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
    if (!encodedTexture)
        return false;
    fixture->textureSize = *encodedTexture;

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
    if (!encodedMaterial)
        return false;
    fixture->materialSize = *encodedMaterial;

    constexpr AnoResourceTypeId textureType = ano::resource_type_id<Texture>();
    constexpr AnoResourceTypeId materialType = ano::resource_type_id<Material>();
    const auto cooker = resource_cooker_create(
        {.firstDerivedAsset = {3}});
    if (!cooker)
        return false;
    auto result = resource_cooker_add(
            *cooker, {2}, materialType, {7},
            {fixture->material, fixture->materialSize});
    if (result)
        result = resource_cooker_add(
            *cooker, {1}, textureType, {7},
            {fixture->texture, fixture->textureSize});
    if (result) {
        const auto revision = resource_cook(*cooker);
        if (revision)
            fixture->revision = *revision;
        else
            result = ano::failure(revision.error());
    }
    resource_cooker_destroy(*cooker);
    return result.has_value();
}

static void check_changed(const AnoResidencyEpoch *epoch,
                          const uint64_t *expected, uint64_t count,
                          const char *message)
{
    bool equal = resource_epoch_changed_count(epoch) == count;
    for (uint64_t i = 0; i < count && equal; ++i) {
        const auto asset = resource_epoch_changed(epoch, i);
        equal = asset && asset->value == expected[i];
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
          "runtime fixtures build as complete revisions");
    if (!fixtures) {
        resource_revision_release(replacement.revision);
        resource_revision_release(original.revision);
        return;
    }

    const auto managerResult = resource_manager_create(original.revision);
    AnoResourceManager *manager = managerResult.value_or(nullptr);
    CHECK(manager != nullptr,
          "manager retains the initial revision");
    if (manager == nullptr) {
        resource_revision_release(replacement.revision);
        resource_revision_release(original.revision);
        return;
    }

    constexpr AnoResourceTypeId textureType = ano::resource_type_id<Texture>();
    constexpr AnoResourceTypeId materialType = ano::resource_type_id<Material>();
    const auto emptyResult = resource_epoch_acquire(manager);
    const AnoResidencyEpoch *empty = emptyResult.value_or(nullptr);
    CHECK(empty && resource_epoch_id(empty).value == 1,
          "manager begins with an immutable empty epoch");
    CHECK(ano::has_error(resource_epoch_resolve(
              empty, {1}, textureType), ANO_RESOURCE_NOT_FOUND),
          "empty epoch exposes no undemanded artifact");

    AnoResourceGoal wrong = {
        .goal = {1},
        .asset = {2},
        .type = textureType,
        .commitGroup = {7},
        .quality = {ANO_RESOURCE_QUALITY_WHOLE},
        .importance = 1.0f,
    };
    CHECK(ano::has_error(resource_goal_set(manager, wrong),
                         ANO_RESOURCE_TYPE_MISMATCH),
          "goal type must match the reflected manifest type");

    const AnoResourceGoal materialGoal = {
        .goal = {1},
        .asset = {2},
        .type = materialType,
        .commitGroup = {7},
        .quality = {ANO_RESOURCE_QUALITY_WHOLE},
        .importance = 1.0f,
    };
    CHECK(resource_goal_set(manager, materialGoal)
          && resource_reconcile(manager),
          "reconciliation materializes the complete commit-group floor");

    const auto residentResult = resource_epoch_acquire(manager);
    const AnoResidencyEpoch *resident = residentResult.value_or(nullptr);
    CHECK(resident && resource_epoch_id(resident).value == 2,
          "complete demand publishes one successor epoch");
    const uint64_t firstChanged[2] = {1, 2};
    check_changed(resident, firstChanged, 2,
                  "first publication reports both new bindings");
    auto resolved = resource_epoch_resolve(resident, {1}, textureType);
    CHECK(resolved && resolved->size == original.textureSize
          && memcmp(resolved->data, original.texture,
                    static_cast<size_t>(resolved->size)) == 0,
          "dependency artifact resolves from the resident epoch");
    resolved = resource_epoch_resolve(resident, {2}, materialType);
    CHECK(resolved && resolved->size == original.materialSize,
          "root artifact resolves from the same epoch");
    CHECK(ano::has_error(resource_epoch_resolve(
              empty, {1}, textureType), ANO_RESOURCE_NOT_FOUND),
          "retained previous epoch does not observe new bindings");

    CHECK(resource_reconcile(manager).has_value(),
          "unchanged reconciliation succeeds");
    const auto unchangedResult = resource_epoch_acquire(manager);
    const AnoResidencyEpoch *unchanged = unchangedResult.value_or(nullptr);
    CHECK(unchanged && resource_epoch_id(unchanged).value == 2,
          "unchanged reconciliation publishes no redundant epoch");
    resource_epoch_release(unchanged);

    auto preparedResult = resource_reload_prepare(
        manager, replacement.revision);
    AnoResourceReload *prepared = preparedResult.value_or(nullptr);
    CHECK(prepared != nullptr
          && resource_reload_has_changes(prepared),
          "replacement prepares as a private changed generation");
    const AnoResidencyEpoch *candidate = resource_reload_epoch(prepared);
    resolved = resource_epoch_resolve(candidate, {1}, textureType);
    CHECK(candidate != nullptr && resolved
          && memcmp(resolved->data, replacement.texture,
                    static_cast<size_t>(resolved->size)) == 0,
          "owner preparation can resolve replacement bytes before publication");
    auto duringPrepareResult = resource_epoch_acquire(manager);
    const AnoResidencyEpoch *duringPrepare =
        duringPrepareResult.value_or(nullptr);
    resolved = resource_epoch_resolve(
        duringPrepare, {1}, textureType);
    CHECK(duringPrepare && resource_epoch_id(duringPrepare).value == 2
          && resolved && memcmp(resolved->data, original.texture,
                    static_cast<size_t>(resolved->size)) == 0,
          "readers retain the published generation while owners prepare");
    resource_epoch_release(duringPrepare);
    resource_reload_abort(prepared);
    duringPrepareResult = resource_epoch_acquire(manager);
    duringPrepare = duringPrepareResult.value_or(nullptr);
    CHECK(duringPrepare && resource_epoch_id(duringPrepare).value == 2,
          "aborting owner preparation preserves the published epoch");
    resource_epoch_release(duringPrepare);

    preparedResult = resource_reload_prepare(
        manager, replacement.revision);
    prepared = preparedResult.value_or(nullptr);
    CHECK(prepared && resource_reload_commit(prepared),
          "owner-approved replacement publishes transactionally");
    const auto reloadedResult = resource_epoch_acquire(manager);
    const AnoResidencyEpoch *reloaded = reloadedResult.value_or(nullptr);
    CHECK(reloaded && resource_epoch_id(reloaded).value == 3,
          "successful reload advances the epoch once");
    const uint64_t reloadChanged[1] = {1};
    check_changed(reloaded, reloadChanged, 1,
                  "reload identifies only changed artifact content");
    resolved = resource_epoch_resolve(reloaded, {1}, textureType);
    CHECK(resolved && memcmp(resolved->data, replacement.texture,
                    static_cast<size_t>(resolved->size)) == 0,
          "successor epoch exposes replacement content");
    resolved = resource_epoch_resolve(resident, {1}, textureType);
    CHECK(resolved && memcmp(resolved->data, original.texture,
                    static_cast<size_t>(resolved->size)) == 0,
          "retained reader keeps the complete previous generation");

    CHECK(resource_goal_remove(manager, {1})
          && ano::has_error(resource_goal_remove(manager, {1}),
                            ANO_RESOURCE_NOT_FOUND)
          && resource_reconcile(manager),
          "removing demand reconciles rather than unloading directly");
    const auto retiredResult = resource_epoch_acquire(manager);
    const AnoResidencyEpoch *retired = retiredResult.value_or(nullptr);
    CHECK(retired && resource_epoch_id(retired).value == 4
          && ano::has_error(resource_epoch_resolve(
              retired, {1}, textureType), ANO_RESOURCE_NOT_FOUND),
          "removed demand publishes a new empty binding set");
    const uint64_t retiredChanged[2] = {1, 2};
    check_changed(retired, retiredChanged, 2,
                  "retirement reports both removed bindings");
    resource_epoch_release(retired);

    resource_manager_destroy(manager);
    resolved = resource_epoch_resolve(reloaded, {1}, textureType);
    CHECK(resolved && memcmp(resolved->data, replacement.texture,
                    static_cast<size_t>(resolved->size)) == 0,
          "acquired epoch outlives its manager and later generations");

    resource_epoch_release(reloaded);
    resource_epoch_release(resident);
    resource_epoch_release(empty);
    resource_revision_release(replacement.revision);
    resource_revision_release(original.revision);
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
