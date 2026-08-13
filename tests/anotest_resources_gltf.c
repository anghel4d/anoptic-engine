/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 * SPDX-License-Identifier: LGPL-3.0
 * Anoptic targets ISO C++26. */

#include <anoptic_render_resources.h>
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
    AnoResourceMutableBytes leftBytes{};
    AnoResourceMutableBytes rightBytes{};
    AnoResourcePack *leftPack = nullptr;
    AnoResourcePack *rightPack = nullptr;
    bool equal = ano_resource_revision_export_pack(left, &leftBytes)
            == ANO_RESOURCE_OK
        && ano_resource_revision_export_pack(right, &rightBytes)
            == ANO_RESOURCE_OK
        && ano_resource_pack_open(
               {leftBytes.data, leftBytes.size}, &leftPack)
            == ANO_RESOURCE_OK
        && ano_resource_pack_open(
               {rightBytes.data, rightBytes.size}, &rightPack)
            == ANO_RESOURCE_OK;
    const AnoResourceManifest *leftManifest = equal
        ? ano_resource_pack_manifest(leftPack) : nullptr;
    const AnoResourceManifest *rightManifest = equal
        ? ano_resource_pack_manifest(rightPack) : nullptr;
    const uint64_t count = equal
        ? ano_resource_manifest_entry_count(leftManifest) : 0;
    equal = equal
        && count == ano_resource_manifest_entry_count(rightManifest);
    for (uint64_t asset = 1; asset <= count && equal; ++asset) {
        AnoResourceManifestEntry lhs{};
        AnoResourceManifestEntry rhs{};
        equal = ano_resource_manifest_find(leftManifest, {asset}, &lhs)
                == ANO_RESOURCE_OK
            && ano_resource_manifest_find(rightManifest, {asset}, &rhs)
                == ANO_RESOURCE_OK
            && lhs.type.value == rhs.type.value
            && lhs.byteSize == rhs.byteSize
            && ano_resource_content_id_equal(lhs.content, rhs.content);
    }
    ano_resource_pack_close(rightPack);
    ano_resource_pack_close(leftPack);
    ano_resource_exported_pack_release(rightBytes);
    ano_resource_exported_pack_release(leftBytes);
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
    const char *alternate = ANO_TEST_BINARY_DIR
        "/resource-incremental/hat_loli.png";
    const char *cursed = ANO_TEST_BINARY_DIR
        "/resource-incremental/cursed.png";
    const bool staged = copy_file(
            ANO_TEST_SOURCE_DIR "/assets/viking_room.gltf", gltf)
        && copy_file(ANO_TEST_SOURCE_DIR "/assets/viking_room.bin", binary)
        && copy_file(ANO_TEST_SOURCE_DIR "/assets/viking_room.png", image)
        && copy_file(ANO_TEST_SOURCE_DIR "/assets/hat_loli.png", alternate)
        && copy_file(ANO_TEST_SOURCE_DIR "/assets/cursed.png", cursed);
    CHECK(staged, "incremental fixture stages exact source files");
    if (!staged)
        return;

    AnoResourceCooker *cooker = nullptr;
    AnoResourceError result = ano_resource_cooker_create(
        {.firstDerivedAsset = {2}}, &cooker);
    const AnoResourceImportRequest request = {{1}, {1}, {1}};
    if (result == ANO_RESOURCE_OK)
        result = ano_resource_source_bind(cooker, {1}, gltf);
    if (result == ANO_RESOURCE_OK)
        result = ano_resource_import(cooker, &request);
    const AnoCookedRevision *first = nullptr;
    if (result == ANO_RESOURCE_OK)
        result = ano_resource_cook(cooker, &first);

    if (result == ANO_RESOURCE_OK)
        result = ano_resource_cooker_begin(cooker);
    if (result == ANO_RESOURCE_OK
        && !copy_file(ANO_TEST_SOURCE_DIR "/assets/hat_loli.png", image))
        result = ANO_RESOURCE_IO_ERROR;
    if (result == ANO_RESOURCE_OK)
        result = ano_resource_import(cooker, &request);
    const AnoCookedRevision *second = nullptr;
    if (result == ANO_RESOURCE_OK)
        result = ano_resource_cook(cooker, &second);
    CHECK(result == ANO_RESOURCE_OK
              && !revision_payloads_equal(first, second),
          "an external image edit changes the cooked public artifacts");

    if (result == ANO_RESOURCE_OK)
        result = ano_resource_cooker_begin(cooker);
    if (result == ANO_RESOURCE_OK && !append_json_whitespace(gltf))
        result = ANO_RESOURCE_IO_ERROR;
    if (result == ANO_RESOURCE_OK)
        result = ano_resource_import(cooker, &request);
    const AnoCookedRevision *equivalent = nullptr;
    if (result == ANO_RESOURCE_OK)
        result = ano_resource_cook(cooker, &equivalent);
    CHECK(result == ANO_RESOURCE_OK
              && revision_payloads_equal(second, equivalent),
          "source-only JSON changes preserve equivalent artifact payloads");

    if (result == ANO_RESOURCE_OK)
        result = ano_resource_cooker_begin(cooker);
    if (result == ANO_RESOURCE_OK)
        result = ano_resource_import(cooker, &request);
    const AnoCookedRevision *unchanged = nullptr;
    if (result == ANO_RESOURCE_OK)
        result = ano_resource_cook(cooker, &unchanged);
    CHECK(result == ANO_RESOURCE_OK
              && revision_payloads_equal(equivalent, unchanged),
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
    const char *relocatedAlternate = ANO_TEST_BINARY_DIR
        "/resource-incremental-relocated/hat_loli.png";
    const char *relocatedCursed = ANO_TEST_BINARY_DIR
        "/resource-incremental-relocated/cursed.png";
    if (result == ANO_RESOURCE_OK
        && (!copy_file(gltf, relocatedGltf)
            || !copy_file(binary, relocatedBinary)
            || !copy_file(image, relocatedImage)
            || !copy_file(alternate, relocatedAlternate)
            || !copy_file(cursed, relocatedCursed)))
        result = ANO_RESOURCE_IO_ERROR;
    if (result == ANO_RESOURCE_OK)
        result = ano_resource_cooker_begin(cooker);
    if (result == ANO_RESOURCE_OK)
        result = ano_resource_source_bind(cooker, {1}, relocatedGltf);
    if (result == ANO_RESOURCE_OK)
        result = ano_resource_import(cooker, &request);
    const AnoCookedRevision *relocated = nullptr;
    if (result == ANO_RESOURCE_OK)
        result = ano_resource_cook(cooker, &relocated);
    CHECK(result == ANO_RESOURCE_OK
              && revision_payloads_equal(unchanged, relocated),
          "source-handle relocation preserves equal public artifacts");

    ano_resource_revision_release(relocated);
    ano_resource_revision_release(unchanged);
    ano_resource_revision_release(equivalent);
    ano_resource_revision_release(second);
    ano_resource_revision_release(first);
    ano_resource_cooker_destroy(cooker);
    (void)remove(image);
    (void)remove(alternate);
    (void)remove(cursed);
    (void)remove(binary);
    (void)remove(gltf);
    (void)remove(relocatedImage);
    (void)remove(relocatedAlternate);
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

    AnoResourceCooker *cooker = nullptr;
    AnoResourceError result = ano_resource_cooker_create(
        {.firstDerivedAsset = {sourceCount + 1}}, &cooker);
    if (result != ANO_RESOURCE_OK)
        return false;
    for (uint32_t source = 0; source < sourceCount; ++source) {
        const int written = snprintf(
            paths[source], sizeof(paths[source]), "%s/%s",
            ANO_TEST_SOURCE_DIR, relativePaths[source]);
        if (written < 0 || (size_t)written >= sizeof(paths[source])) {
            ano_resource_cooker_destroy(cooker);
            return false;
        }
        result = ano_resource_source_bind(
            cooker, {source + 1},
            ANO_TEST_SOURCE_DIR "/assets/viking_room.glb");
        if (result == ANO_RESOURCE_OK)
            result = ano_resource_source_bind(
                cooker, {source + 1}, paths[source]);
        if (result != ANO_RESOURCE_OK) {
            ano_resource_cooker_destroy(cooker);
            return false;
        }
        const AnoResourceImportRequest request = {
            .source = {source + 1},
            .rootAsset = {source + 1},
            .commitGroup = {source + 1},
        };
        result = ano_resource_import(cooker, &request);
        if (result != ANO_RESOURCE_OK) {
            fprintf(stderr, "import %s: %s\n", relativePaths[source],
                    ano_resource_error_string(result));
            ano_resource_cooker_destroy(cooker);
            return false;
        }
    }

    const AnoCookedRevision *revision = nullptr;
    result = ano_resource_cook(cooker, &revision);
    AnoResourceMutableBytes cooked = {};
    if (result == ANO_RESOURCE_OK)
        result = ano_resource_revision_export_pack(revision, &cooked);
    ano_resource_revision_release(revision);
    ano_resource_cooker_destroy(cooker);
    if (result != ANO_RESOURCE_OK) {
        ano_resource_exported_pack_release(cooked);
        return false;
    }

    AnoResourcePack *pack = nullptr;
    result = ano_resource_pack_open({cooked.data, cooked.size}, &pack);
    ano_resource_exported_pack_release(cooked);
    if (result != ANO_RESOURCE_OK)
        return false;

    const AnoResourceManifest *manifest = ano_resource_pack_manifest(pack);
    const uint64_t entryCount = ano_resource_manifest_entry_count(manifest);
    for (uint64_t asset = 1; asset <= entryCount; ++asset) {
        AnoResourceManifestEntry entry = {};
        if (ano_resource_manifest_find(manifest, {asset}, &entry)
            != ANO_RESOURCE_OK) {
            ano_resource_pack_close(pack);
            return false;
        }
        if (entry.type.value == ano::resource_type_id<Texture>().value)
            ++counts->textures;
        else if (entry.type.value == ano::resource_type_id<Material>().value)
            ++counts->materials;
        else if (entry.type.value == ano::resource_type_id<Mesh>().value)
            ++counts->meshes;
        else if (entry.type.value == ano::resource_type_id<Scene>().value)
            ++counts->scenes;
    }

    for (uint32_t source = 0; source < sourceCount
                              && result == ANO_RESOURCE_OK; ++source) {
        uint64_t sceneSize = 0;
        result = ano_resource_pack_read(
            pack, {source + 1}, {nullptr, 0}, &sceneSize);
        if (result != ANO_RESOURCE_BUFFER_TOO_SMALL || sceneSize == 0)
            break;
        uint8_t *sceneBytes = static_cast<uint8_t *>(malloc((size_t)sceneSize));
        if (sceneBytes == nullptr) {
            result = ANO_RESOURCE_OUT_OF_MEMORY;
            break;
        }
        result = ano_resource_pack_read(
            pack, {source + 1}, {sceneBytes, sceneSize}, &sceneSize);
        if (result == ANO_RESOURCE_OK) {
            const ano::DecodeResult<Scene> decoded =
                ano::decode<Scene>({sceneBytes, sceneSize});
            if (decoded.error == ANO_RESOURCE_OK) {
                scenes[source].renderables =
                    decoded.view.value.renderables.count;
                scenes[source].lights = decoded.view.value.lights.count;
                SceneRenderable *renderables = static_cast<SceneRenderable *>(
                    calloc((size_t)scenes[source].renderables,
                           sizeof(SceneRenderable)));
                if (scenes[source].renderables != 0 && renderables == nullptr)
                    result = ANO_RESOURCE_OUT_OF_MEMORY;
                else if (ano::resolve_span(
                             decoded.view, decoded.view.value.renderables,
                             renderables, scenes[source].renderables)
                         != ANO_RESOURCE_OK)
                    result = ANO_RESOURCE_NON_CANONICAL;
                for (uint64_t i = 0; i < scenes[source].renderables
                                     && result == ANO_RESOURCE_OK; ++i)
                    if (renderables[i].mesh.id.value == 0)
                        result = ANO_RESOURCE_NON_CANONICAL;
                free(renderables);
            } else {
                result = decoded.error;
            }
        }
        free(sceneBytes);
    }
    ano_resource_pack_close(pack);
    return result == ANO_RESOURCE_OK;
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
