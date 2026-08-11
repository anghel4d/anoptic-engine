/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 * SPDX-License-Identifier: LGPL-3.0 */

#include <anoptic_render_resources.h>
#include <anoptic_resources_cook.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

    AnoResourceMutableBytes cooked = {};
    result = ano_resource_cook(cooker, &cooked);
    ano_resource_cooker_destroy(cooker);
    if (result != ANO_RESOURCE_OK) {
        ano_resource_cooked_pack_release(cooked);
        return false;
    }

    AnoResourcePack *pack = nullptr;
    result = ano_resource_pack_open({cooked.data, cooked.size}, &pack);
    ano_resource_cooked_pack_release(cooked);
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
    return failures == 0 ? 0 : 1;
}
