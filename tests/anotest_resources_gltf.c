/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 * SPDX-License-Identifier: LGPL-3.0
 * Anoptic targets ISO C++26. */

#include <anoptic_render_resources.h>

using namespace ano;
#include <anoptic_resources_cook.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32)
#include <direct.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

using namespace ano::asset_schema;

static int failures = 0;

#define CHECK(condition, message)                                                \
    do {                                                                         \
        if (!(condition)) {                                                       \
            fprintf(stderr, "FAIL: %s\n", message);                              \
            ++failures;                                                           \
        }                                                                         \
    } while (0)

typedef struct ImportedCounts {
    uint64_t textures;
    uint64_t materials;
    uint64_t meshes;
    uint64_t scenes;
} ImportedCounts;

typedef struct SceneCounts {
    uint64_t renderables;
    uint64_t lights;
} SceneCounts;

static bool copy_file(const char *source, const char *destination)
{
    FILE *input = fopen(source, "rb");
    FILE *output = input == nullptr ? nullptr : fopen(destination, "wb");
    uint8_t buffer[64 * 1024];
    bool copied = input != nullptr && output != nullptr;
    while (copied) {
        const size_t read = fread(buffer, 1, sizeof(buffer), input);
        if (read != 0 && fwrite(buffer, 1, read, output) != read)
            copied = false;
        if (read != sizeof(buffer)) {
            copied = copied && feof(input) && !ferror(input);
            break;
        }
    }
    if (output != nullptr)
        copied = fclose(output) == 0 && copied;
    if (input != nullptr)
        copied = fclose(input) == 0 && copied;
    return copied;
}

static bool append_json_whitespace(const char *path)
{
    FILE *file = fopen(path, "ab");
    if (file == nullptr)
        return false;
    const uint8_t whitespace[] = {'\n', ' '};
    const bool written = fwrite(whitespace, 1, sizeof(whitespace), file)
        == sizeof(whitespace);
    return fclose(file) == 0 && written;
}

static bool revision_payloads_equal(const AnoCookedRevision *left,
                                    const AnoCookedRevision *right)
{
    const auto leftBytes = resource_revision_export_pack(left);
    const auto rightBytes = resource_revision_export_pack(right);
    auto leftOpened = leftBytes
        ? resource_pack_open({leftBytes->data, leftBytes->size})
        : ResourceResult<AnoResourcePack *>(failure(leftBytes.error()));
    auto rightOpened = rightBytes
        ? resource_pack_open({rightBytes->data, rightBytes->size})
        : ResourceResult<AnoResourcePack *>(failure(rightBytes.error()));
    AnoResourcePack *leftPack = leftOpened.value_or(nullptr);
    AnoResourcePack *rightPack = rightOpened.value_or(nullptr);
    bool equal = leftPack && rightPack;
    const AnoResourceManifest *leftManifest = equal
        ? resource_pack_manifest(leftPack) : nullptr;
    const AnoResourceManifest *rightManifest = equal
        ? resource_pack_manifest(rightPack) : nullptr;
    const uint64_t count = equal
        ? resource_manifest_entry_count(leftManifest) : 0;
    equal = equal
        && count == resource_manifest_entry_count(rightManifest);
    for (uint64_t asset = 1; asset <= count && equal; ++asset) {
        const auto lhs = resource_manifest_find(leftManifest, {asset});
        const auto rhs = resource_manifest_find(rightManifest, {asset});
        equal = lhs && rhs && lhs->type.value == rhs->type.value
            && lhs->byteSize == rhs->byteSize
            && resource_content_id_equal(lhs->content, rhs->content);
    }
    resource_pack_close(rightPack);
    resource_pack_close(leftPack);
    if (rightBytes)
        resource_exported_pack_release(*rightBytes);
    if (leftBytes)
        resource_exported_pack_release(*leftBytes);
    return equal;
}

static void test_incremental_external_image(void)
{
    const char *directory = ANO_TEST_BINARY_DIR "/resource-incremental";
#if defined(_WIN32)
    (void)_mkdir(directory);
#else
    (void)mkdir(directory, 0700);
#endif
    const char *gltf = ANO_TEST_BINARY_DIR
        "/resource-incremental/viking_room.gltf";
    const char *binary = ANO_TEST_BINARY_DIR
        "/resource-incremental/viking_room.bin";
    const char *image = ANO_TEST_BINARY_DIR
        "/resource-incremental/viking_room.png";
    const char *cursed = ANO_TEST_BINARY_DIR
        "/resource-incremental/cursed.png";
    const bool staged = copy_file(
            ANO_TEST_SOURCE_DIR "/assets/viking_room.gltf", gltf)
        && copy_file(ANO_TEST_SOURCE_DIR "/assets/viking_room.bin", binary)
        && copy_file(ANO_TEST_SOURCE_DIR "/assets/viking_room.png", image)
        && copy_file(ANO_TEST_SOURCE_DIR "/assets/cursed.png", cursed);
    CHECK(staged, "incremental fixture stages exact source files");
    if (!staged)
        return;

    const auto created = resource_cooker_create(
        {.firstDerivedAsset = {2}});
    AnoResourceCooker *cooker = created.value_or(nullptr);
    ResourceResult<> result = created
        ? ResourceResult<>{} : ResourceResult<>(failure(created.error()));
    const auto publish = [&](const AnoCookedRevision *&revision) {
        const auto cooked = resource_cook(cooker);
        if (!cooked)
            return ResourceResult<>(failure(cooked.error()));
        revision = *cooked;
        return ResourceResult<>{};
    };
    const AnoResourceImportRequest request = {{1}, {1}, {1}};
    if (result)
        result = resource_source_bind(cooker, {1}, gltf);
    if (result)
        result = resource_import(cooker, request);
    const AnoCookedRevision *first = nullptr;
    if (result)
        result = publish(first);

    if (result)
        result = resource_cooker_begin(cooker);
    if (result
        && !copy_file(ANO_TEST_SOURCE_DIR "/assets/cursed.png", image))
        result = failure(ANO_RESOURCE_IO_ERROR);
    if (result)
        result = resource_import(cooker, request);
    const AnoCookedRevision *second = nullptr;
    if (result)
        result = publish(second);
    CHECK(result && !revision_payloads_equal(first, second),
          "an external image edit changes the cooked public artifacts");

    if (result)
        result = resource_cooker_begin(cooker);
    if (result && !append_json_whitespace(gltf))
        result = failure(ANO_RESOURCE_IO_ERROR);
    if (result)
        result = resource_import(cooker, request);
    const AnoCookedRevision *equivalent = nullptr;
    if (result)
        result = publish(equivalent);
    CHECK(result && revision_payloads_equal(second, equivalent),
          "source-only JSON changes preserve equivalent artifact payloads");

    if (result)
        result = resource_cooker_begin(cooker);
    if (result)
        result = resource_import(cooker, request);
    const AnoCookedRevision *unchanged = nullptr;
    if (result)
        result = publish(unchanged);
    CHECK(result && revision_payloads_equal(equivalent, unchanged),
          "an unchanged source graph preserves every public artifact");

    const char *relocatedDirectory = ANO_TEST_BINARY_DIR
        "/resource-incremental-relocated";
#if defined(_WIN32)
    (void)_mkdir(relocatedDirectory);
#else
    (void)mkdir(relocatedDirectory, 0700);
#endif
    const char *relocatedGltf = ANO_TEST_BINARY_DIR
        "/resource-incremental-relocated/viking_room.gltf";
    const char *relocatedBinary = ANO_TEST_BINARY_DIR
        "/resource-incremental-relocated/viking_room.bin";
    const char *relocatedImage = ANO_TEST_BINARY_DIR
        "/resource-incremental-relocated/viking_room.png";
    const char *relocatedCursed = ANO_TEST_BINARY_DIR
        "/resource-incremental-relocated/cursed.png";
    if (result
        && (!copy_file(gltf, relocatedGltf)
            || !copy_file(binary, relocatedBinary)
            || !copy_file(image, relocatedImage)
            || !copy_file(cursed, relocatedCursed)))
        result = failure(ANO_RESOURCE_IO_ERROR);
    if (result)
        result = resource_cooker_begin(cooker);
    if (result)
        result = resource_source_bind(cooker, {1}, relocatedGltf);
    if (result)
        result = resource_import(cooker, request);
    const AnoCookedRevision *relocated = nullptr;
    if (result)
        result = publish(relocated);
    CHECK(result && revision_payloads_equal(unchanged, relocated),
          "source-handle relocation preserves equal public artifacts");

    resource_revision_release(relocated);
    resource_revision_release(unchanged);
    resource_revision_release(equivalent);
    resource_revision_release(second);
    resource_revision_release(first);
    resource_cooker_destroy(cooker);
    (void)remove(image);
    (void)remove(cursed);
    (void)remove(binary);
    (void)remove(gltf);
    (void)remove(relocatedImage);
    (void)remove(relocatedCursed);
    (void)remove(relocatedBinary);
    (void)remove(relocatedGltf);
#if defined(_WIN32)
    (void)_rmdir(directory);
    (void)_rmdir(relocatedDirectory);
#else
    (void)rmdir(directory);
    (void)rmdir(relocatedDirectory);
#endif
}

static bool import_sources(const char *const *relativePaths,
                           uint32_t sourceCount, ImportedCounts *counts,
                           SceneCounts *scenes)
{
    if (sourceCount == 0 || sourceCount > 8)
        return false;
    char paths[8][1024] = {};

    const auto created = resource_cooker_create(
        {.firstDerivedAsset = {sourceCount + 1}});
    if (!created)
        return false;
    AnoResourceCooker *cooker = *created;
    ResourceResult<> result{};
    for (uint32_t source = 0; source < sourceCount; ++source) {
        const int written = snprintf(
            paths[source], sizeof(paths[source]), "%s/%s",
            ANO_TEST_SOURCE_DIR, relativePaths[source]);
        if (written < 0 || (size_t)written >= sizeof(paths[source])) {
            resource_cooker_destroy(cooker);
            return false;
        }
        result = resource_source_bind(
            cooker, {source + 1},
            ANO_TEST_SOURCE_DIR "/assets/viking_room.glb");
        if (result)
            result = resource_source_bind(
                cooker, {source + 1}, paths[source]);
        if (!result) {
            resource_cooker_destroy(cooker);
            return false;
        }
        const AnoResourceImportRequest request = {
            .source = {source + 1},
            .rootAsset = {source + 1},
            .commitGroup = {source + 1},
        };
        result = resource_import(cooker, request);
        if (!result) {
            fprintf(stderr, "import %s: %s\n", relativePaths[source],
                    resource_error_string(result.error()));
            resource_cooker_destroy(cooker);
            return false;
        }
    }

    const auto revisionResult = resource_cook(cooker);
    const AnoCookedRevision *revision = revisionResult.value_or(nullptr);
    const auto cooked = revision
        ? resource_revision_export_pack(revision)
        : ResourceResult<AnoResourceMutableBytes>(
              failure(revisionResult.error()));
    resource_revision_release(revision);
    resource_cooker_destroy(cooker);
    if (!cooked) {
        return false;
    }

    const auto opened = resource_pack_open(
        {cooked->data, cooked->size});
    resource_exported_pack_release(*cooked);
    if (!opened)
        return false;
    AnoResourcePack *pack = *opened;

    const AnoResourceManifest *manifest = resource_pack_manifest(pack);
    const uint64_t entryCount = resource_manifest_entry_count(manifest);
    for (uint64_t asset = 1; asset <= entryCount; ++asset) {
        const auto entry = resource_manifest_find(manifest, {asset});
        if (!entry) {
            resource_pack_close(pack);
            return false;
        }
        if (entry->type.value == ano::resource_type_id<Texture>().value)
            ++counts->textures;
        else if (entry->type.value == ano::resource_type_id<Material>().value)
            ++counts->materials;
        else if (entry->type.value == ano::resource_type_id<Mesh>().value)
            ++counts->meshes;
        else if (entry->type.value == ano::resource_type_id<Scene>().value)
            ++counts->scenes;
    }

    for (uint32_t source = 0; source < sourceCount && result; ++source) {
        const auto sceneBytes = resource_pack_view(pack, {source + 1});
        if (!sceneBytes || sceneBytes->size == 0) {
            result = sceneBytes
                ? ResourceResult<>(failure(ANO_RESOURCE_NON_CANONICAL))
                : ResourceResult<>(failure(sceneBytes.error()));
            break;
        }
        const auto decoded = ano::decode<Scene>(*sceneBytes);
        if (!decoded) {
            result = failure(decoded.error());
            break;
        }
        scenes[source].renderables = decoded->value.renderables.count;
        scenes[source].lights = decoded->value.lights.count;
        SceneRenderable *renderables = static_cast<SceneRenderable *>(
            calloc((size_t)scenes[source].renderables,
                   sizeof(SceneRenderable)));
        if (scenes[source].renderables != 0 && renderables == nullptr)
            result = failure(ANO_RESOURCE_OUT_OF_MEMORY);
        else if (!ano::resolve_span(
                     *decoded, decoded->value.renderables, renderables,
                     scenes[source].renderables))
            result = failure(ANO_RESOURCE_NON_CANONICAL);
        for (uint64_t i = 0; i < scenes[source].renderables && result; ++i)
            if (renderables[i].mesh.id.value == 0)
                result = failure(ANO_RESOURCE_NON_CANONICAL);
        free(renderables);
    }
    resource_pack_close(pack);
    return result.has_value();
}

static void test_viking_glb(void)
{
    const char *paths[] = {"assets/viking_room.glb"};
    ImportedCounts counts = {};
    SceneCounts scenes[1] = {};
    CHECK(import_sources(paths, 1, &counts, scenes),
          "Viking Room GLB imports, cooks, opens, and resolves");
    CHECK(counts.textures == 1 && counts.materials == 1
              && counts.meshes == 1 && counts.scenes == 1
              && scenes[0].renderables == 1 && scenes[0].lights == 0,
          "Viking Room GLB canonical graph matches its pinned semantics");
}

static void test_current_vertical(void)
{
    const char *paths[] = {
        "assets/viking_room.gltf",
        "assets/GlassHurricaneCandleHolder.gltf",
        "assets/sponza/2.0/Sponza/glTF/Sponza.gltf",
    };
    ImportedCounts counts = {};
    SceneCounts scenes[3] = {};
    CHECK(import_sources(paths, 3, &counts, scenes),
          "the complete current scene imports into one authenticated pack");
    CHECK(counts.textures == 73 && counts.materials == 28
              && counts.meshes == 106 && counts.scenes == 3,
          "the current scene pins canonical artifact counts");
    CHECK(scenes[0].renderables == 1 && scenes[0].lights == 0,
          "Viking Room scene semantics are pinned");
    CHECK(scenes[1].renderables == 2 && scenes[1].lights == 0,
          "candle holder scene semantics are pinned");
    CHECK(scenes[2].renderables == 103 && scenes[2].lights == 0,
          "Sponza scene semantics are pinned");
}

int main(void)
{
    test_viking_glb();
    test_current_vertical();
    test_incremental_external_image();
    return failures == 0 ? 0 : 1;
}
