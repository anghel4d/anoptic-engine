/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0
 * Anoptic targets ISO C++26. */
/*  == Anoptic Game Engine v0.0000001 == */

#include <anoptic_memory.h>

using namespace ano;
#include <stdio.h>
#include <stdlib.h>
#include <anoptic_atomic.h>
#include <string.h>
#include <math.h>
#include "anoptic_time.h"
#include "anoptic_threads.h"
#include "anoptic_filesystem.h"
#include "anoptic_log_crash.h"   // logger + crash blackbox

#ifndef HEADLESS_BUILD
#include <anoptic_render.h>
#include <anoptic_text.h> // logic shapes; bake is render-owned
#include <anoptic_ui.h>
// MusicGen (ANOPTIC_ENGINE_MUSIC): composer in the audio callback; logic uses the bridge only.
#include <anoptic_audio.h>
#include <anoptic_music.h>
#include <anoptic_synth.h>
#include <anoptic_render_resources.h>
#include "vulkan_backend/render_api.h"
#include <vulkan/vulkan.h>
#ifndef GLFW_INCLUDE_VULKAN
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#endif
#endif // !HEADLESS_BUILD

/* Variables */

#ifndef HEADLESS_BUILD
// Sole render-command producer. Render world owns main.
// Stop and join before unInitVulkan() destroys the bridge.
static atomic_bool g_logicShouldStop = false;
static atomic_bool g_resourceReloadRequested = false;
static atomic_bool g_frameCaptureRequested = false;

namespace {

using ano::asset_schema::Scene;
using ano::asset_schema::SceneLight;
using ano::asset_schema::SceneLightType;

inline constexpr ano::AssetRef<ano::asset_schema::Scene> VIKING_ROOM = {{1}};
inline constexpr ano::AssetRef<ano::asset_schema::Scene> CANDLE_HOLDER = {{2}};
inline constexpr ano::AssetRef<ano::asset_schema::Scene> SPONZA = {{3}};
inline constexpr ano::AssetRef<ano::asset_schema::Scene> STATIC_LIGHTING = {{4}};
inline constexpr ano::AssetRef<ano::asset_schema::Scene> CANDLE_LIGHTING = {{5}};
inline constexpr ano::SourceRef<ano::asset_schema::Scene> VIKING_SOURCE = {{1}};
inline constexpr ano::SourceRef<ano::asset_schema::Scene> CANDLE_SOURCE = {{2}};
inline constexpr ano::SourceRef<ano::asset_schema::Scene> SPONZA_SOURCE = {{3}};
inline constexpr AnoResourceCommitGroupId SCENE_GROUP = {1};

struct StartupSources final {
    const char *viking;
    const char *candle;
    const char *sponza;
};

inline constexpr StartupSources DEFAULT_SOURCES = {
    "viking_room.gltf",
    "GlassHurricaneCandleHolder.gltf",
    "sponza/2.0/Sponza/glTF/Sponza.gltf",
};

inline constexpr SceneLight STATIC_LIGHTS[] = {
    {{1,0,0,0, 0,1,0,0, .2f,1,0,0, 0,0,0,1},
     {1.0f,.96f,.9f}, 2.5f, 0, 0, 0,
     SceneLightType::directional, true},
    {{1,0,0,0, 0,1,0,0, 0,0,1,0, 0,1.5f,1.2f,1},
     {1.0f,.95f,.8f}, 5.0f, 10.0f, 0, 0,
     SceneLightType::point, true},
    {{1,0,0,0, 0,1,0,0, 0,0,1,0, -2.0f,2.0f,-1.0f,1},
     {.4f,.6f,1.0f}, 4.0f, 10.0f, 0, 0,
     SceneLightType::point, true},
    {{1,0,0,0, 0,1,0,0, 0,0,1,0, 2.0f,.5f,0,1},
     {1.0f,.3f,.3f}, 3.5f, 10.0f, 0, 0,
     SceneLightType::point, true},
    {{1,0,0,0, 0,1,0,0, 0,0,1,0, 0,-1.0f,1.0f,1},
     {.3f,1.0f,.8f}, 2.0f, 10.0f, 0, 0,
     SceneLightType::point, true},
    {{1,0,0,0, 0,1,0,0, 0,1,0,0, 0,4.0f,0,1},
     {1,1,1}, 20.0f, 12.0f, .2617994f, .4363323f,
     SceneLightType::spot, true},
};

inline constexpr SceneLight CANDLE_LIGHTS[] = {
    {{1,0,0,0, 0,1,0,0, 0,0,1,0, .6f,.3f,0,1},
     {1.0f,.5f,.15f}, 6.0f, 4.0f, 0, 0,
     SceneLightType::point, false},
    {{1,0,0,0, 0,1,0,0, 0,0,1,0, -.6f,.3f,0,1},
     {.2f,.8f,1.0f}, 6.0f, 4.0f, 0, 0,
     SceneLightType::point, false},
    {{1,0,0,0, 0,1,0,0, 0,0,1,0, 0,.8f,0,1},
     {1.0f,.2f,.8f}, 5.0f, 4.0f, 0, 0,
     SceneLightType::point, false},
    {{1,0,0,0, 0,1,0,0, -.7f,.7f,0,0, 0,1.2f,0,1},
     {.5f,1.0f,.6f}, 12.0f, 6.0f, .3175604f, .5548110f,
     SceneLightType::spot, false},
    {{1,0,0,0, 0,1,0,0, .7f,.7f,0,0, 0,1.2f,0,1},
     {1.0f,.7f,.3f}, 12.0f, 6.0f, .3175604f, .5548110f,
     SceneLightType::spot, false},
};

struct StartupResources final {
    AnoResourceCooker *cooker;
    AnoResourceManager *manager;
};

enum ReloadWorkerState : uint32_t {
    RELOAD_WORKER_IDLE,
    RELOAD_WORKER_REQUESTED,
    RELOAD_WORKER_RUNNING,
    RELOAD_WORKER_READY,
    RELOAD_WORKER_ACTIVE,
    RELOAD_WORKER_RECLAIM,
    RELOAD_WORKER_STOP,
};

struct ReloadWorker final {
    AnoResourceManager *manager;
    AnoResourceCooker *cooker;
    StartupSources sources;
    AnoRenderResourcePublication *publication;
    mi_heap_t *heap;
    AnoResourceError result;
    anothread_t thread;
    anothread_mutex_t mutex;
    anothread_cond_t wake;
    ReloadWorkerState state;
    uint64_t requestedGeneration;
    uint64_t completedGeneration;
    bool discardReady;
};

template<size_t Count>
AnoResourceError add_lighting(
    AnoResourceCooker *cooker, ano::AssetRef<Scene> asset,
    const SceneLight (&lights)[Count])
{
    const Scene scene = {
        .renderables = {0, 0},
        .lights = {0, Count},
    };
    return ano::cook_artifact(
        cooker, asset, SCENE_GROUP,
        ano::ArtifactSource<Scene>{
            &scene,
            {reinterpret_cast<const uint8_t *>(lights), sizeof(lights)},
        });
}

AnoResourceError cook_startup_revision(
    AnoResourceCooker *cooker, const StartupSources& sources,
    const AnoCookedRevision **revision)
{
    if (cooker == nullptr || revision == nullptr)
        return ANO_RESOURCE_INVALID_ARGUMENT;
    *revision = nullptr;
    AnoResourceError result = ano_resource_cooker_begin(cooker);
    struct SourceImport final {
        AnoResourceSourceId source;
        const char *path;
        AnoAssetId root;
    };
    const SourceImport imports[] = {
        {VIKING_SOURCE.id, sources.viking, VIKING_ROOM.id},
        {CANDLE_SOURCE.id, sources.candle, CANDLE_HOLDER.id},
        {SPONZA_SOURCE.id, sources.sponza, SPONZA.id},
    };
    for (const SourceImport& source : imports) {
        if (result == ANO_RESOURCE_OK)
            result = ano_resource_source_bind(
                cooker, source.source, source.path);
        if (result == ANO_RESOURCE_OK) {
            const AnoResourceImportRequest request = {
                source.source, source.root, SCENE_GROUP,
            };
            result = ano_resource_import(cooker, &request);
        }
    }
    if (result == ANO_RESOURCE_OK)
        result = add_lighting(cooker, STATIC_LIGHTING, STATIC_LIGHTS);
    if (result == ANO_RESOURCE_OK)
        result = add_lighting(cooker, CANDLE_LIGHTING, CANDLE_LIGHTS);

    if (result == ANO_RESOURCE_OK)
        result = ano_resource_cook(cooker, revision);
    return result;
}

AnoResourceError create_startup_resources(StartupResources *startup)
{
    if (startup == nullptr)
        return ANO_RESOURCE_INVALID_ARGUMENT;
    *startup = {};
    // Startup scenes occupy ids 1-5.
    AnoResourceError result = ano_resource_cooker_create(
        {.firstDerivedAsset = {6}}, &startup->cooker);
    const AnoCookedRevision *revision = nullptr;
    if (result == ANO_RESOURCE_OK)
        result = cook_startup_revision(
            startup->cooker, DEFAULT_SOURCES, &revision);
    if (result == ANO_RESOURCE_OK)
        result = ano_resource_manager_create(revision, &startup->manager);
    ano_resource_revision_release(revision);
    const AnoResourceGoal goals[] = {
        {{1}, VIKING_ROOM.id, ano::resource_type_id<ano::asset_schema::Scene>(),
         SCENE_GROUP, {ANO_RESOURCE_QUALITY_WHOLE}, 1.0f},
        {{2}, CANDLE_HOLDER.id, ano::resource_type_id<ano::asset_schema::Scene>(),
         SCENE_GROUP, {ANO_RESOURCE_QUALITY_WHOLE}, 1.0f},
        {{3}, SPONZA.id, ano::resource_type_id<ano::asset_schema::Scene>(),
         SCENE_GROUP, {ANO_RESOURCE_QUALITY_WHOLE}, 1.0f},
        {{4}, STATIC_LIGHTING.id, ano::resource_type_id<ano::asset_schema::Scene>(),
         SCENE_GROUP, {ANO_RESOURCE_QUALITY_WHOLE}, 1.0f},
        {{5}, CANDLE_LIGHTING.id, ano::resource_type_id<ano::asset_schema::Scene>(),
         SCENE_GROUP, {ANO_RESOURCE_QUALITY_WHOLE}, 1.0f},
    };
    for (const AnoResourceGoal& goal : goals)
        if (result == ANO_RESOURCE_OK)
            result = ano_resource_goal_set(startup->manager, goal);
    if (result == ANO_RESOURCE_OK)
        result = ano_resource_reconcile(startup->manager);
    if (result != ANO_RESOURCE_OK) {
        ano_resource_manager_destroy(startup->manager);
        ano_resource_cooker_destroy(startup->cooker);
        *startup = {};
    }
    return result;
}

AnoResourceError prepare_startup_resources_reload(
    AnoResourceCooker *cooker, AnoResourceManager *manager,
    const StartupSources& sources,
    AnoResourceReload **reload)
{
    if (reload == nullptr)
        return ANO_RESOURCE_INVALID_ARGUMENT;
    *reload = nullptr;
    const AnoCookedRevision *revision = nullptr;
    AnoResourceError result = cook_startup_revision(
        cooker, sources, &revision);
    if (result == ANO_RESOURCE_OK)
        result = ano_resource_reload_prepare(manager, revision, reload);
    ano_resource_revision_release(revision);
    return result;
}

void *prepare_reload_worker(void *argument)
{
    mi_thread_set_in_threadpool();
    ReloadWorker& worker = *static_cast<ReloadWorker *>(argument);
    ano_mutex_lock(&worker.mutex);
    for (;;) {
        while (worker.state != RELOAD_WORKER_REQUESTED
               && worker.state != RELOAD_WORKER_STOP)
            ano_thread_cond_wait(&worker.wake, &worker.mutex);
        if (worker.state == RELOAD_WORKER_STOP) break;
        worker.state = RELOAD_WORKER_RUNNING;
        AnoResourceReload *reload = nullptr;
        mi_heap_t *heap = nullptr;
        AnoRenderResourcePublication *publication = nullptr;
        AnoResourceError result = ANO_RESOURCE_OK;
        uint64_t generation = 0;
        for (;;) {
            const StartupSources sources = worker.sources;
            generation = worker.requestedGeneration;
            ano_mutex_unlock(&worker.mutex);
            heap = ano_heap_create();
            result = heap
                ? prepare_startup_resources_reload(
                    worker.cooker, worker.manager, sources, &reload)
                : ANO_RESOURCE_OUT_OF_MEMORY;
            if (result == ANO_RESOURCE_OK)
                result = ano_render_resources_prepare_reload(
                    reload, heap, &publication);
            else
                ano_resource_reload_abort(reload);
            reload = nullptr;
            ano_mutex_lock(&worker.mutex);
            if (generation == worker.requestedGeneration)
                break;
            ano_mutex_unlock(&worker.mutex);
            ano_render_resources_cancel_reload(publication);
            publication = nullptr;
            ano_heap_destroy(heap);
            heap = nullptr;
            ano_mutex_lock(&worker.mutex);
        }
        worker.heap = heap;
        worker.publication = publication;
        worker.result = result;
        worker.completedGeneration = generation;
        worker.state = RELOAD_WORKER_READY;
        ano_thread_cond_broadcast(&worker.wake);
        while (worker.state != RELOAD_WORKER_RECLAIM
               && worker.state != RELOAD_WORKER_STOP)
            ano_thread_cond_wait(&worker.wake, &worker.mutex);
        const bool stop = worker.state == RELOAD_WORKER_STOP;
        const bool discard = worker.discardReady;
        heap = worker.heap;
        publication = discard ? worker.publication : nullptr;
        worker.heap = nullptr;
        worker.publication = nullptr;
        worker.discardReady = false;
        ano_mutex_unlock(&worker.mutex);
        ano_render_resources_cancel_reload(publication);
        ano_heap_destroy(heap);
        ano_mutex_lock(&worker.mutex);
        if (stop) break;
        worker.state = worker.requestedGeneration > worker.completedGeneration
            ? RELOAD_WORKER_REQUESTED : RELOAD_WORKER_IDLE;
        ano_thread_cond_broadcast(&worker.wake);
    }
    worker.state = RELOAD_WORKER_STOP;
    ano_thread_cond_broadcast(&worker.wake);
    ano_mutex_unlock(&worker.mutex);
    return nullptr;
}

bool reload_worker_start(ReloadWorker& worker)
{
    worker.state = RELOAD_WORKER_IDLE;
    if (ano_mutex_init(&worker.mutex, nullptr) != 0) return false;
    if (ano_thread_cond_init(&worker.wake, nullptr) != 0) {
        ano_mutex_destroy(&worker.mutex);
        return false;
    }
    if (ano_thread_create(&worker.thread, nullptr,
                          prepare_reload_worker, &worker) != 0) {
        ano_thread_cond_destroy(&worker.wake);
        ano_mutex_destroy(&worker.mutex);
        return false;
    }
    return true;
}

bool reload_worker_request(ReloadWorker& worker,
                           const StartupSources& sources)
{
    ano_mutex_lock(&worker.mutex);
    const bool accepted = worker.state != RELOAD_WORKER_STOP;
    if (accepted) {
        worker.sources = sources;
        ++worker.requestedGeneration;
        if (worker.state == RELOAD_WORKER_IDLE) {
            worker.state = RELOAD_WORKER_REQUESTED;
            ano_thread_cond_signal(&worker.wake);
        } else if (worker.state == RELOAD_WORKER_RUNNING) {
            ano_resource_cooker_cancel(worker.cooker);
        } else if (worker.state == RELOAD_WORKER_READY) {
            worker.discardReady = true;
            worker.state = RELOAD_WORKER_RECLAIM;
            ano_thread_cond_signal(&worker.wake);
        }
    }
    ano_mutex_unlock(&worker.mutex);
    return accepted;
}

bool reload_worker_take(ReloadWorker& worker, AnoResourceError *result,
                        AnoRenderResourcePublication **publication)
{
    ano_mutex_lock(&worker.mutex);
    const bool ready = worker.state == RELOAD_WORKER_READY;
    if (ready) {
        *result = worker.result;
        *publication = worker.publication;
        worker.state = RELOAD_WORKER_ACTIVE;
    }
    ano_mutex_unlock(&worker.mutex);
    return ready;
}

void reload_worker_reclaim(ReloadWorker& worker)
{
    ano_mutex_lock(&worker.mutex);
    if (worker.state == RELOAD_WORKER_ACTIVE
        || worker.state == RELOAD_WORKER_READY) {
        worker.state = RELOAD_WORKER_RECLAIM;
        ano_thread_cond_signal(&worker.wake);
    }
    ano_mutex_unlock(&worker.mutex);
}

void reload_worker_stop(ReloadWorker& worker)
{
    AnoRenderResourcePublication *unclaimed = nullptr;
    ano_mutex_lock(&worker.mutex);
    while (worker.state == RELOAD_WORKER_REQUESTED
           || worker.state == RELOAD_WORKER_RUNNING)
        ano_thread_cond_wait(&worker.wake, &worker.mutex);
    if (worker.state == RELOAD_WORKER_READY) {
        unclaimed = worker.publication;
        worker.state = RELOAD_WORKER_ACTIVE;
    }
    ano_mutex_unlock(&worker.mutex);
    ano_render_resources_cancel_reload(unclaimed);
    if (unclaimed) reload_worker_reclaim(worker);

    ano_mutex_lock(&worker.mutex);
    while (worker.state == RELOAD_WORKER_RECLAIM)
        ano_thread_cond_wait(&worker.wake, &worker.mutex);
    worker.state = RELOAD_WORKER_STOP;
    ano_thread_cond_signal(&worker.wake);
    ano_mutex_unlock(&worker.mutex);
    ano_thread_join(worker.thread, nullptr);
    ano_thread_cond_destroy(&worker.wake);
    ano_mutex_destroy(&worker.mutex);
}

} // namespace

/* Scene Composition. Logic owns the scene. */

// Logic emits creates. Render world owns GPU assets.

// in:  bridge, c
// out: true once enqueued; false on shutdown (command dropped)
// inv: wait observes g_logicShouldStop
[[nodiscard]] static bool submit_blocking(AnoRenderBridge* bridge, const RenderCommand* c) {
	while (!ano_render_submit(bridge, c)) {
		if (atomic_load(&g_logicShouldStop)) return false;
		ano_sleep(1000);
	}
	return true;
}

// One renderable per primitive. Shared motion; speed is +Y rate for spin/orbit.
// Returns first render_id. Advances *nextId.
#define SPAWN_ASSET_MAX_PRIMS 256u
static uint32_t spawn_asset(
                            AnoRenderBridge* bridge, uint32_t* nextId,
                            ano::AssetRef<ano::asset_schema::Scene> asset,
                            const mat4 root, AnoMotionType motion, float speed) {
	AnoRenderableDesc descs[SPAWN_ASSET_MAX_PRIMS];
	uint32_t n = anoRenderAssetPrimitives(asset.id, root, descs, SPAWN_ASSET_MAX_PRIMS);
	if (n == 0u) { ano_log(ANO_WARN, "Producer: asset %llu has no primitives; nothing spawned.", (unsigned long long)asset.id.value); return UINT32_MAX; }
	if (n > SPAWN_ASSET_MAX_PRIMS) { ano_log(ANO_WARN, "Producer: asset %llu has %u primitives; spawning only the first %u.", (unsigned long long)asset.id.value, n, SPAWN_ASSET_MAX_PRIMS); n = SPAWN_ASSET_MAX_PRIMS; }
	uint32_t first = *nextId;
	for (uint32_t i = 0; i < n; i++) {
		RenderCommand c = { .kind = RCMD_CREATE, .render_id = (*nextId)++,
			.mesh_index = descs[i].mesh_index, .material_index = descs[i].material_index,
			.resource_asset = descs[i].resource_asset,
			.resource_primitive = descs[i].resource_primitive,
			.light_index = ANO_RENDER_NO_LIGHT };
		memcpy(c.transform, descs[i].transform, sizeof(mat4));
		memcpy(c.resource_root, root, sizeof(mat4));
		c.motion.type = (uint32_t)motion;
		if (motion == ANO_MOTION_SPIN || motion == ANO_MOTION_ORBIT) c.motion.p0.v[1] = speed; // about +Y
		if (!submit_blocking(bridge, &c)) return UINT32_MAX; // shutdown mid-spawn
	}
	return first;
}

// Fallback cube at transform. Advances *nextId. Returns render_id.
static uint32_t spawn_box(AnoRenderBridge* bridge, uint32_t* nextId, const mat4 transform) {
	uint32_t id = (*nextId)++;
	RenderCommand c = { .kind = RCMD_CREATE, .render_id = id,
		.mesh_index = anoRenderFallbackMesh(), .material_index = anoRenderDefaultMaterial(),
		.light_index = ANO_RENDER_NO_LIGHT };
	memcpy(c.transform, transform, sizeof(mat4));
	c.motion.type = (uint32_t)ANO_MOTION_STATIC;
	if (!submit_blocking(bridge, &c)) return UINT32_MAX; // shutdown mid-spawn
	return id;
}

// Pose: pos = col3, forward = -col2 (localDir default -Z). light_index is the static palette row.
// Casters take static-region shadow frustums (dir/spot 1, point 6).
static uint32_t spawn_light_entity(AnoRenderBridge* bridge, uint32_t* nextId, const mat4 transform,
                                   uint32_t light_index, const RenderLightParams* params,
                                   AnoMotionType motion, float speed) {
	uint32_t id = (*nextId)++;
	RenderCommand c = { .kind = RCMD_CREATE, .render_id = id,
		.mesh_index = ANO_RENDER_NO_MESH, .material_index = 0u,
		.light_index = light_index, .light = *params };
	memcpy(c.transform, transform, sizeof(mat4));
	c.motion.type = (uint32_t)motion;
	if (motion == ANO_MOTION_SPIN || motion == ANO_MOTION_ORBIT) c.motion.p0.v[1] = speed; // about +Y
	if (!submit_blocking(bridge, &c)) return UINT32_MAX; // shutdown mid-spawn
	return id;
}

static void spawn_static_lighting(AnoRenderBridge *bridge, uint32_t *nextId)
{
	mat4 identity = {{1,0,0,0},{0,1,0,0},{0,0,1,0},{0,0,0,1}};
	AnoSceneLightDesc lights[32];
	uint32_t count = anoRenderAssetLights(
		STATIC_LIGHTING.id, identity, lights, sizeof lights / sizeof *lights);
	if (count > sizeof lights / sizeof *lights)
		count = sizeof lights / sizeof *lights;
	for (uint32_t i = 0; i < count; ++i)
		spawn_light_entity(
			bridge, nextId, lights[i].transform, i, &lights[i].light,
			ANO_MOTION_STATIC, 0.0f);
}

static void attach_candle_lighting(AnoRenderBridge *bridge, uint32_t candleSlot)
{
	if (candleSlot == UINT32_MAX)
		return;
	mat4 identity = {{1,0,0,0},{0,1,0,0},{0,0,1,0},{0,0,0,1}};
	AnoSceneLightDesc lights[16];
	uint32_t count = anoRenderAssetLights(
		CANDLE_LIGHTING.id, identity, lights, sizeof lights / sizeof *lights);
	if (count > sizeof lights / sizeof *lights)
		count = sizeof lights / sizeof *lights;
	for (uint32_t i = 0; i < count; ++i) {
		RenderLightParams params = lights[i].light;
		params.localDir[0] = -lights[i].transform[2][0];
		params.localDir[1] = -lights[i].transform[2][1];
		params.localDir[2] = -lights[i].transform[2][2];
		const uint32_t id = 100u + i;
		while (!ano_render_light_attach(
				bridge, id, candleSlot, &params,
				lights[i].transform[3][0], lights[i].transform[3][1],
				lights[i].transform[3][2])) {
			if (atomic_load(&g_logicShouldStop))
				return;
			ano_sleep(1000);
		}
	}
}

// Compose once. render_id and static light_index are this producer's namespaces.
static void spawn_scene(AnoRenderBridge* bridge) {
	uint32_t nextId = 0u;

	// Viking room: Z-up glTF -> Y-up (-90 X).
	mat4 vikingRoot = {{1,0,0,0},{0,0,-1,0},{0,1,0,0},{0,0,0,1}};
	spawn_asset(bridge, &nextId, VIKING_ROOM, vikingRoot, ANO_MOTION_SPIN, 1.0f);

	// First holder anchors the decorative lights.
	mat4 candle1 = {{1,0,0,0},{0,1,0,0},{0,0,1,0},{2.0f,0,0,1}};
	mat4 candle2 = {{1,0,0,0},{0,1,0,0},{0,0,1,0},{2.2f,0,0,1}};
	uint32_t candleSlot = spawn_asset(bridge, &nextId, CANDLE_HOLDER, candle1, ANO_MOTION_ORBIT, 0.5f);
	spawn_asset(bridge, &nextId, CANDLE_HOLDER, candle2, ANO_MOTION_ORBIT, 0.5f);

	// Sponza is already Y-up.
	mat4 sponzaRoot = {{1,0,0,0},{0,1,0,0},{0,0,1,0},{0,0,0,1}};
	spawn_asset(bridge, &nextId, SPONZA, sponzaRoot, ANO_MOTION_STATIC, 0.0f);

	// Sun marker, near the overhead light aim.
	mat4 sunMarker = {{0.2f,0,0,0},{0,0.2f,0,0},{0,0,0.2f,0},{2.59f,5.18f,1.55f,1}};
	spawn_box(bridge, &nextId, sunMarker);

	spawn_static_lighting(bridge, &nextId);
	attach_candle_lighting(bridge, candleSlot);
}

/* HUD Text */

// Shape on this thread; ship named blocks. text_id is this producer's namespace.
#define HUD_TEXT_TITLE   1u
#define HUD_TEXT_NOTICE  2u
#define HUD_TEXT_CAM     3u
#define HUD_TEXT_UNICODE 4u
#define HUD_TEXT_HOMER   5u
#define HUD_TEXT_CAP     128u

static AnoRenderSubmitResult hud_text_submit(AnoRenderBridge* bridge, uint32_t text_id,
                                             AnoGlyphInstance* inst, uint32_t shaped) {
	if (shaped > HUD_TEXT_CAP) shaped = HUD_TEXT_CAP;
	return ano_render_text_set(bridge, text_id, inst, shaped);
}

/* HUD UI */

// Layout, style, and hit-test here. Renderer gets prim blocks only.
#define HUD_UI_BAR   1u
#define HUD_UI_MENU  2u
#define HUD_UI_GCAP  192u

struct HudCommonStyle final {
	ano::UiColor shadow;
	ano::UiColor white;
	ano::UiColor rim;
	ano::UiColor label;
	ano::UiStops<2> plate;
};

static constexpr HudCommonStyle HUD_COMMON_STYLE = {
	.shadow = ano::ui_srgb(0.00f, 0.00f, 0.00f, 0.60f),
	.white = ano::ui_srgb(1.00f, 1.00f, 1.00f, 1.00f),
	.rim = ano::ui_srgb(0.62f, 0.65f, 0.70f, 1.00f),
	.label = ano::ui_srgb(0.92f, 0.94f, 0.97f, 1.00f),
	.plate = ano::ui_stops(
		ano::ui_stop(0.0f, 0.17f, 0.18f, 0.22f, 0.97f),
		ano::ui_stop(1.0f, 0.09f, 0.10f, 0.13f, 0.97f)),
};

struct MenuStyle final {
	ano::UiColor button;
	ano::UiColor buttonHot;
	ano::UiColor buttonRim;
	ano::UiColor title;
	ano::UiColor glow;
};

static constexpr MenuStyle MENU_STYLE = {
	.button = ano::ui_srgb(0.22f, 0.24f, 0.30f, 1.00f),
	.buttonHot = ano::ui_srgb(0.28f, 0.45f, 0.80f, 1.00f),
	.buttonRim = ano::ui_srgb(0.75f, 0.80f, 0.88f, 0.90f),
	.title = ano::ui_srgb(1.00f, 0.80f, 0.35f, 1.00f),
	.glow = ano::ui_srgb(0.25f, 0.45f, 0.85f, 0.00f),
};

struct BarStyle final {
	ano::UiColor shadow;
	ano::UiColor plate;
	ano::UiColor rim;
	ano::UiColor label;
};

static constexpr BarStyle BAR_STYLE = {
	.shadow = ano::ui_srgb(0.00f, 0.00f, 0.00f, 0.50f),
	.plate = ano::ui_srgb(0.10f, 0.11f, 0.13f, 0.92f),
	.rim = ano::ui_srgb(0.50f, 0.54f, 0.60f, 1.00f),
	.label = ano::ui_srgb(0.88f, 0.90f, 0.94f, 1.00f),
};

// Overlay logical units; shared by render and hit-test.
typedef struct MenuLayout {
	float panel[4];      // minX minY maxX maxY
	float button[3][4];
} MenuLayout;

static void menu_layout(float vpW, float vpH, MenuLayout* out)
{
	float x0 = vpW * 0.5f - 160.0f, y0 = vpH * 0.5f - 150.0f;
	out->panel[0] = x0; out->panel[1] = y0;
	out->panel[2] = x0 + 320.0f; out->panel[3] = y0 + 300.0f;
	for (int i = 0; i < 3; i++) {
		float by = y0 + 84.0f + (float)i * 64.0f;
		out->button[i][0] = x0 + 20.0f;  out->button[i][1] = by;
		out->button[i][2] = x0 + 300.0f; out->button[i][3] = by + 48.0f;
	}
}

static int menu_hit(const MenuLayout* m, float cx, float cy)
{
	for (int i = 0; i < 3; i++)
		if (cx >= m->button[i][0] && cx <= m->button[i][2]
		    && cy >= m->button[i][1] && cy <= m->button[i][3])
			return i;
	return -1;
}

// Baseline at ~0.7 em, optical center in the rect.
static void ui_label(AnoUiBuilder* b, const AnoFontBake* bake, anostr_t text, float sizePx,
                     const float rect[4], const float color[4],
                     AnoGlyphInstance* glyphs, uint32_t* gcount)
{
	if (bake == NULL || *gcount >= HUD_UI_GCAP) return;
	float w, h;
	ano_text_measure(bake, text, sizePx, &w, &h);
	float ox = rect[0] + ((rect[2] - rect[0]) - w) * 0.5f;
	float baseline = rect[1] + ((rect[3] - rect[1]) + 0.70f * sizePx) * 0.5f;
	uint32_t first = *gcount;
	uint32_t n = ano_text_shape(bake, text, sizePx, (float[2]){ ox, baseline }, color,
	                            glyphs + first, HUD_UI_GCAP - first, NULL);
	if (n > HUD_UI_GCAP - first) n = HUD_UI_GCAP - first;
	*gcount = first + n;
	float lo[2] = { ox - 2.0f, baseline - bake->ascender * sizePx - 2.0f };
	float hi[2] = { ox + w + 2.0f, baseline - bake->descender * sizePx + 2.0f };
	float white[4] = { 1, 1, 1, 1 };
	ano_ui_glyphs(b, lo, hi, first, n, white, ANO_UI_REF_NONE, 0);
}

static AnoRenderSubmitResult submit_menu(AnoRenderBridge* bridge, const AnoFontBake* bake, const MenuLayout* m,
                                         bool visible, int hovered, uint32_t optionsCount)
{
	if (!visible)
		return ano_render_ui_clear(bridge, HUD_UI_MENU);
	AnoUiPrim prims[24];
	AnoUiPaint paints[2];
	AnoUiStop stops[4];
	uint32_t curves[128];
	AnoGlyphInstance glyphs[HUD_UI_GCAP];
	uint32_t gcount = 0;
	AnoUiBuilder b;
	ano_ui_builder_init(&b, prims, 24, NULL, 0, paints, 2, stops, 4);
	ano_ui_builder_curves(&b, curves, 128);
	const auto& common = HUD_COMMON_STYLE;
	const auto& style = MENU_STYLE;
	uint32_t plateGrad = ano::ui_paint_linear(&b, (float[2]){ m->panel[0], m->panel[1] },
	                                        (float[2]){ m->panel[0], m->panel[3] }, common.plate);
	float r12[4] = { 12, 12, 12, 12 }, r8[4] = { 8, 8, 8, 8 };
	ano_ui_shadow(&b, (float[2]){ m->panel[0] + 6, m->panel[1] + 10 },
	              (float[2]){ m->panel[2] + 6, m->panel[3] + 10 }, 12.0f, 9.0f, common.shadow.rgba,
	              ANO_UI_REF_NONE, 0);
	ano_ui_rrect(&b, &m->panel[0], &m->panel[2], r12, common.white.rgba, 0.0f,
	             plateGrad, ANO_UI_REF_NONE, 0);
	ano_ui_rrect(&b, &m->panel[0], &m->panel[2], r12, common.rim.rgba, 2.0f,
	             ANO_UI_REF_NONE, ANO_UI_REF_NONE, 0);
	float titleRect[4] = { m->panel[0], m->panel[1] + 14, m->panel[2], m->panel[1] + 58 };
	ui_label(&b, bake, anostr_lit("MENU"), 26.0f, titleRect, style.title.rgba, glyphs, &gcount);
	for (int i = 0; i < 3; i++) {
		bool hot = hovered == i;
		if (hot)
			ano_ui_shadow(&b, &m->button[i][0], &m->button[i][2], 8.0f, 8.0f, style.glow.rgba,
			              ANO_UI_REF_NONE, ANO_UI_BLEND_ADD);
		ano_ui_rrect(&b, &m->button[i][0], &m->button[i][2], r8,
		             hot ? style.buttonHot.rgba : style.button.rgba, 0.0f,
		             ANO_UI_REF_NONE, ANO_UI_REF_NONE, 0);
		ano_ui_rrect(&b, &m->button[i][0], &m->button[i][2], r8, style.buttonRim.rgba, hot ? 2.0f : 1.0f,
		             ANO_UI_REF_NONE, ANO_UI_REF_NONE, 0);
		char text[32];
		int len;
		if (i == 1 && optionsCount > 0)
			len = snprintf(text, sizeof text, "OPTIONS (%u)", optionsCount);
		else
			len = snprintf(text, sizeof text, "%s", (const char*[]){ "RESUME", "OPTIONS", "QUIT" }[i]);
		if (len > 0)
			ui_label(&b, bake, anostr_view(text, (size_t)len), 20.0f, m->button[i],
			         common.label.rgba, glyphs, &gcount);
	}
	// RESUME play-triangle (curve transport).
	float rb0 = m->button[0][0], rcy = 0.5f * (m->button[0][1] + m->button[0][3]);
	AnoUiPathSeg play[3] = {
		{ ANO_UI_SEG_MOVE, { rb0 + 22.0f, rcy - 9.0f, 0.0f, 0.0f } },
		{ ANO_UI_SEG_LINE, { rb0 + 38.0f, rcy, 0.0f, 0.0f } },
		{ ANO_UI_SEG_LINE, { rb0 + 22.0f, rcy + 9.0f, 0.0f, 0.0f } },
	};
	ano_ui_path_fill(&b, play, 3, common.label.rgba, ANO_UI_REF_NONE, ANO_UI_REF_NONE, 0);
	return ano_render_ui_set(bridge, HUD_UI_MENU, 128, &b, glyphs, gcount);
}

/* Music World */

#if defined(ANOPTIC_ENGINE_MUSIC)
// Bring-up on main before the producer; tear down after join. No submit may race destruction.
// Composer runs in the mixer callback, two bars ahead. Logic uses the audio bridge only.

#define MUSIC_RATE 48000u
#define MUSIC_SEED 2718u

static AnoSynth       *g_synth;
static AnoMusicEngine *g_music;

static void music_config(AnoMusicConfig *c)
{
	*c = ano_music_config_default();
	c->hasMapper = true;
	c->mapper = ano_mapping_table_electronic();
	c->hasDramaturg = true;
	c->dramaturg = ano_dramaturg_config_default();
	c->phraseGroove = true;
	c->cadenceRit = 0.03;
	c->wanderPhrases = 6;
	c->form.cadential64 = c->form.periods = c->form.hypermeter = true;
	c->form.bassInversions = c->form.split64 = true;
	c->texture.doubling = c->texture.animate = c->texture.imitation = true;
	c->texture.rotate = c->texture.counter = true;
	c->ties.anacrusis = c->ties.suspension = c->ties.syncopation = true;
	c->clock.codetta = c->clock.extension = c->clock.elision = true;
	c->melody.planApex = c->melody.counterpoint = true;
	c->useChains = c->performChains = true;
	// Panel start affect: calm, slightly bright.
	c->valence = 0.30f;
	c->energy = 0.35f;
	c->tension = 0.20f;
}

static void music_world_stop(bool drain);

// false: silent run. Fail path calls music_world_stop(false); g_synth/g_music stay NULL.
static bool music_world_start(void)
{
	bool started = [&]() -> bool {
	AnoMusicConfig cfg;
	music_config(&cfg);

	AnoSynthDesc synthDesc = { .sampleRate = MUSIC_RATE };
	g_synth = ano_synth_create(&synthDesc);
	g_music = ano_music_create(&cfg, MUSIC_SEED);
	if (g_synth == NULL || g_music == NULL)
		return false;
	if (!ano_synth_attach_music(g_synth, g_music))
		return false;

	AnoAudioBusDesc layout[ANO_SYNTH_CONSOLE_BUSES];
	uint32_t buses = ano_synth_console_layout(layout, ANO_SYNTH_CONSOLE_BUSES);

	// Synth seams: generate, control, poll, stats, commands.
	AnoAudioConfig acfg = {
		.sampleRate = MUSIC_RATE,
		.busCount = buses,
		.busLayout = layout,
		.generator         = ano_synth_generator,
		.generatorUser     = g_synth,
		.generatorControl  = ano_synth_control,
		.generatorPoll     = ano_synth_poll,
		.generatorStats    = ano_synth_stats,
		.generatorCommands = ano_synth_commands,
	};
	if (!ano_audio_init(&acfg))
		return false;

	AnoAudioBridge *ab = anoAudioBridge();
	AnoAudioOfflineEvent setup[64];
	uint32_t n = ano_synth_console_setup(setup, 64);
	// 1000 tries at 1 ms; stuck ring fails setup.
	for (uint32_t i = 0; i < n; i++) {
		uint32_t spin = 0;
		while (!ano_audio_submit(ab, &setup[i].cmd)) {
			if (++spin >= 1000u) {
				ano_log(ANO_WARN, "Music: audio command ring full for 1 s; console setup abandoned.");
				return false;
			}
			ano_sleep(1000);
		}
	}

	// Start transport a few blocks ahead of the playhead. No publish in ~1 s: no seed.
	AnoAudioTelemetry t;
	bool haveTelem = false;
	for (uint32_t spin = 0; spin < 200u; spin++) {
		if (ano_audio_acquire_telemetry(ab, &t)) { haveTelem = true; break; }
		ano_sleep(5000);
	}
	if (!haveTelem) {
		ano_log(ANO_WARN, "Music: no mixer telemetry after 1 s; transport not started.");
		return false;
	}
	ano_synth_transport_start(g_synth, (t.blockIndex + 8u) * (uint64_t)t.blockFrames);
	ano_log(ANO_INFO, "Music: composing live at %u Hz (seed %u).", MUSIC_RATE,
	        (unsigned)MUSIC_SEED);
	return true;
	}();
	if (!started)
		music_world_stop(false); // unwind; transport never started
	return started;
}

// drain: stop transport and wait for mixer tails. Start-unwind passes false.
// Idempotent from any partial state.
static void music_world_stop(bool drain)
{
	if (drain && g_synth != NULL) {
		ano_synth_transport_stop(g_synth);
		ano_sleep(50000); // mixer stop + tails
	}
	ano_audio_shutdown();
	ano_synth_destroy(g_synth);
	ano_music_destroy(g_music);
	g_synth = NULL;
	g_music = NULL;
}
#endif

/* Music Panel */

// Pad XY is valence (brightness) x energy; slider is tension. No middle layer.

#define HUD_UI_MUSIC 3u

struct MusicStyle final {
	ano::UiColor title;
	ano::UiColor track;
	ano::UiColor knob;
	ano::UiColor dim;
	ano::UiColor hair;
	ano::UiStops<3> axis;
};

static constexpr MusicStyle MUSIC_STYLE = {
	.title = ano::ui_srgb(0.55f, 0.85f, 1.00f, 1.00f),
	.track = ano::ui_srgb(0.10f, 0.11f, 0.14f, 1.00f),
	.knob = ano::ui_srgb(0.96f, 0.97f, 1.00f, 1.00f),
	.dim = ano::ui_srgb(0.55f, 0.60f, 0.68f, 1.00f),
	.hair = ano::ui_srgb(0.80f, 0.85f, 0.92f, 0.22f),
	.axis = ano::ui_stops(
		ano::ui_stop(0.0f, 0.10f, 0.13f, 0.26f, 1.00f),
		ano::ui_stop(0.5f, 0.16f, 0.17f, 0.20f, 1.00f),
		ano::ui_stop(1.0f, 0.34f, 0.26f, 0.13f, 1.00f)),
};

enum { MUS_DRAG_NONE = 0, MUS_DRAG_XY, MUS_DRAG_TENSION };

typedef struct MusicLayout {
	float panel[4];  // x0, y0, x1, y1
	float pad[4];    // valence x energy square
	float slider[4]; // tension track
} MusicLayout;

static void music_layout(float vpW, float vpH, MusicLayout* o)
{
	(void)vpH;
	const float w = 300.0f, h = 436.0f, pad = 220.0f;
	// Below profiling OSD
	float x0 = vpW - w - 24.0f, y0 = 104.0f;
	o->panel[0] = x0;      o->panel[1] = y0;
	o->panel[2] = x0 + w;  o->panel[3] = y0 + h;
	float px = x0 + (w - pad) * 0.5f, py = y0 + 62.0f;
	o->pad[0] = px;        o->pad[1] = py;
	o->pad[2] = px + pad;  o->pad[3] = py + pad;
	o->slider[0] = px;              o->slider[1] = py + pad + 52.0f;
	o->slider[2] = px + pad;        o->slider[3] = py + pad + 52.0f + 14.0f;
}

static bool in_rect(const float r[4], float x, float y)
{
	return x >= r[0] && x <= r[2] && y >= r[1] && y <= r[3];
}

static int music_hit(const MusicLayout* m, float x, float y)
{
	if (in_rect(m->pad, x, y))
		return MUS_DRAG_XY;
	// Hit-rect is taller than the track.
	float grab[4] = { m->slider[0] - 10.0f, m->slider[1] - 12.0f,
	                  m->slider[2] + 10.0f, m->slider[3] + 12.0f };
	if (in_rect(grab, x, y))
		return MUS_DRAG_TENSION;
	return MUS_DRAG_NONE;
}

static float clamp01f(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

// Filled from AEVT_MUSIC_BAR on the audible downbeat, not when composed.
typedef struct MusicState {
	float valence, energy, tension;
	int   bar, keyTonic, mode, chordDegree;
	bool  isCadence;
	uint32_t genUs;   // audio-thread compose cost (us)
	uint64_t flashUntil; // cadence-rim deadline (us)
} MusicState;

static const char *const PC_NAMES[12] = { "C", "C#", "D", "D#", "E", "F",
                                          "F#", "G", "G#", "A", "A#", "B" };
static_assert(sizeof PC_NAMES / sizeof *PC_NAMES == 12, "PC_NAMES is a pitch-class table");
static const char *const ROMAN[8] = { "-", "I", "II", "III", "IV", "V", "VI", "VII" };

// Drag maps the cursor; values clamp outside the control.
static void music_drag_apply(const MusicLayout* m, int drag, float x, float y,
                             MusicState* st)
{
	if (drag == MUS_DRAG_XY) {
		float u = (x - m->pad[0]) / (m->pad[2] - m->pad[0]);
		float v = (m->pad[3] - y) / (m->pad[3] - m->pad[1]);
		st->valence = clamp01f(u) * 2.0f - 1.0f; // -1 .. +1
		st->energy = clamp01f(v);
	} else if (drag == MUS_DRAG_TENSION) {
		st->tension = clamp01f((x - m->slider[0]) / (m->slider[2] - m->slider[0]));
	}
}

static AnoRenderSubmitResult submit_music(AnoRenderBridge* bridge, const AnoFontBake* bake,
                                          const MusicLayout* m, bool visible, const MusicState* st,
                                          int hovered, uint64_t now)
{
	if (!visible)
		return ano_render_ui_clear(bridge, HUD_UI_MUSIC);

	AnoUiPrim prims[40];
	AnoUiPaint paints[4];
	AnoUiStop stops[8];
	AnoGlyphInstance glyphs[HUD_UI_GCAP];
	uint32_t gcount = 0;
	AnoUiBuilder b;
	ano_ui_builder_init(&b, prims, 40, NULL, 0, paints, 4, stops, 8);

	const auto& common = HUD_COMMON_STYLE;
	const auto& style = MUSIC_STYLE;
	uint32_t plateGrad = ano::ui_paint_linear(&b, (float[2]){ m->panel[0], m->panel[1] },
	                                        (float[2]){ m->panel[0], m->panel[3] }, common.plate);
	float r12[4] = { 12, 12, 12, 12 }, r6[4] = { 6, 6, 6, 6 };
	ano_ui_shadow(&b, (float[2]){ m->panel[0] + 6, m->panel[1] + 10 },
	              (float[2]){ m->panel[2] + 6, m->panel[3] + 10 }, 12.0f, 9.0f, common.shadow.rgba,
	              ANO_UI_REF_NONE, 0);

	// 0.5 s ADD cadence flash.
	if (now < st->flashUntil) {
		float k = (float)(st->flashUntil - now) / 500000.0f;
		float pulse[4];
		ano_ui_color_srgb((float[4]){ 0.35f * k, 0.75f * k, 1.00f * k, 0.0f }, pulse);
		ano_ui_shadow(&b, (float[2]){ m->panel[0] - 2, m->panel[1] - 2 },
		              (float[2]){ m->panel[2] + 2, m->panel[3] + 2 }, 14.0f, 12.0f, pulse,
		              ANO_UI_REF_NONE, ANO_UI_BLEND_ADD);
	}
	ano_ui_rrect(&b, &m->panel[0], &m->panel[2], r12, common.white.rgba, 0.0f, plateGrad,
	             ANO_UI_REF_NONE, 0);
	ano_ui_rrect(&b, &m->panel[0], &m->panel[2], r12, common.rim.rgba, 2.0f, ANO_UI_REF_NONE,
	             ANO_UI_REF_NONE, 0);
	float titleRect[4] = { m->panel[0], m->panel[1] + 12, m->panel[2], m->panel[1] + 52 };
	ui_label(&b, bake, anostr_lit("MUSIC"), 24.0f, titleRect, style.title.rgba, glyphs, &gcount);

	// Axis pad: cold-dark -> warm-bright.
	uint32_t axisGrad = ano::ui_paint_linear(&b, (float[2]){ m->pad[0], m->pad[1] },
	                                       (float[2]){ m->pad[2], m->pad[1] }, style.axis);
	ano_ui_rrect(&b, &m->pad[0], &m->pad[2], r6, common.white.rgba, 0.0f, axisGrad,
	             ANO_UI_REF_NONE, 0);
	ano_ui_rrect(&b, &m->pad[0], &m->pad[2], r6,
	             hovered == MUS_DRAG_XY ? common.rim.rgba : style.dim.rgba,
	             hovered == MUS_DRAG_XY ? 2.0f : 1.0f, ANO_UI_REF_NONE, ANO_UI_REF_NONE, 0);

	float kx = m->pad[0] + (st->valence * 0.5f + 0.5f) * (m->pad[2] - m->pad[0]);
	float ky = m->pad[3] - st->energy * (m->pad[3] - m->pad[1]);
	ano_ui_rrect(&b, (float[2]){ m->pad[0] + 1, ky - 0.5f },
	             (float[2]){ m->pad[2] - 1, ky + 0.5f }, (float[4]){ 0, 0, 0, 0 }, style.hair.rgba,
	             0.0f, ANO_UI_REF_NONE, ANO_UI_REF_NONE, 0);
	ano_ui_rrect(&b, (float[2]){ kx - 0.5f, m->pad[1] + 1 },
	             (float[2]){ kx + 0.5f, m->pad[3] - 1 }, (float[4]){ 0, 0, 0, 0 }, style.hair.rgba,
	             0.0f, ANO_UI_REF_NONE, ANO_UI_REF_NONE, 0);
	float gr = 10.0f + 16.0f * st->energy;
	float lit[4];
	ano_ui_color_srgb((float[4]){ 0.30f * (0.3f + st->energy), 0.70f * (0.3f + st->energy),
	                              1.00f * (0.3f + st->energy), 0.0f }, lit);
	ano_ui_shadow(&b, (float[2]){ kx - gr, ky - gr }, (float[2]){ kx + gr, ky + gr },
	              gr, 9.0f, lit, ANO_UI_REF_NONE, ANO_UI_BLEND_ADD);
	float kr[4] = { 7, 7, 7, 7 };
	ano_ui_rrect(&b, (float[2]){ kx - 7, ky - 7 }, (float[2]){ kx + 7, ky + 7 }, kr, style.knob.rgba,
	             0.0f, ANO_UI_REF_NONE, ANO_UI_REF_NONE, 0);

	float axisRow[4] = { m->pad[0], m->pad[3] + 2, m->pad[2], m->pad[3] + 26 };
	ui_label(&b, bake, anostr_lit("dark  <  brightness  >  bright"), 15.0f, axisRow, style.dim.rgba,
	         glyphs, &gcount);

	ano_ui_rrect(&b, &m->slider[0], &m->slider[2], r6, style.track.rgba, 0.0f, ANO_UI_REF_NONE,
	             ANO_UI_REF_NONE, 0);
	float fillX = m->slider[0] + st->tension * (m->slider[2] - m->slider[0]);
	if (fillX > m->slider[0] + 1.0f) {
		float hot[4];
		ano_ui_color_srgb((float[4]){ 0.85f, 0.30f + 0.30f * (1.0f - st->tension), 0.30f,
		                              1.0f }, hot);
		ano_ui_rrect(&b, &m->slider[0], (float[2]){ fillX, m->slider[3] }, r6, hot, 0.0f,
		             ANO_UI_REF_NONE, ANO_UI_REF_NONE, 0);
	}
	ano_ui_rrect(&b, &m->slider[0], &m->slider[2], r6,
	             hovered == MUS_DRAG_TENSION ? common.rim.rgba : style.dim.rgba,
	             hovered == MUS_DRAG_TENSION ? 2.0f : 1.0f, ANO_UI_REF_NONE,
	             ANO_UI_REF_NONE, 0);
	float sy = 0.5f * (m->slider[1] + m->slider[3]);
	ano_ui_rrect(&b, (float[2]){ fillX - 6, sy - 10 }, (float[2]){ fillX + 6, sy + 10 },
	             (float[4]){ 5, 5, 5, 5 }, style.knob.rgba, 0.0f, ANO_UI_REF_NONE, ANO_UI_REF_NONE, 0);
	char tenText[40];
	int tl = snprintf(tenText, sizeof tenText, "tension  %.2f", (double)st->tension);
	float tenRow[4] = { m->slider[0], m->slider[1] - 26, m->slider[2], m->slider[1] - 4 };
	if (tl > 0)
		ui_label(&b, bake, anostr_view(tenText, (size_t)tl), 15.0f, tenRow, style.dim.rgba, glyphs,
		         &gcount);

	// Last AEVT_MUSIC_BAR.
	char line1[64], line2[64];
	int n1, n2;
	if (st->bar >= 0) {
		n1 = snprintf(line1, sizeof line1, "bar %d  %s %s  %s", st->bar,
		              PC_NAMES[st->keyTonic % 12], ano_mode_name((AnoMode)st->mode),
		              ROMAN[(st->chordDegree >= 0 && st->chordDegree <= 7)
		                        ? st->chordDegree : 0]);
		n2 = snprintf(line2, sizeof line2, "composed in %u us on the audio thread",
		              st->genUs);
	} else {
		n1 = snprintf(line1, sizeof line1, "waiting for the first bar");
		n2 = snprintf(line2, sizeof line2, " ");
	}
	float row1[4] = { m->panel[0], m->slider[3] + 16, m->panel[2], m->slider[3] + 42 };
	float row2[4] = { m->panel[0], m->slider[3] + 40, m->panel[2], m->slider[3] + 62 };
	if (n1 > 0)
		ui_label(&b, bake, anostr_view(line1, (size_t)n1), 17.0f,
		         row1, st->isCadence ? style.title.rgba : common.label.rgba, glyphs, &gcount);
	if (n2 > 0)
		ui_label(&b, bake, anostr_view(line2, (size_t)n2), 13.0f, row2, style.dim.rgba, glyphs,
		         &gcount);

	return ano_render_ui_set(bridge, HUD_UI_MUSIC, 96, &b, glyphs, gcount);
}

// Bottom-left; resubmit when vpH changes.
static AnoRenderSubmitResult submit_bar(AnoRenderBridge* bridge, const AnoFontBake* bake, float vpH)
{
	AnoUiPrim prims[8];
	AnoGlyphInstance glyphs[HUD_UI_GCAP];
	uint32_t gcount = 0;
	AnoUiBuilder b;
	ano_ui_builder_init(&b, prims, 8, NULL, 0, NULL, 0, NULL, 0);
	const auto& style = BAR_STYLE;
	float rect[4] = { 24.0f, vpH - 68.0f, 24.0f + 420.0f, vpH - 24.0f };
	float r10[4] = { 10, 10, 10, 10 };
	ano_ui_shadow(&b, (float[2]){ rect[0] + 4, rect[1] + 6 }, (float[2]){ rect[2] + 4, rect[3] + 6 },
	              10.0f, 6.0f, style.shadow.rgba, ANO_UI_REF_NONE, 0);
	ano_ui_rrect(&b, &rect[0], &rect[2], r10, style.plate.rgba, 0.0f, ANO_UI_REF_NONE, ANO_UI_REF_NONE, 0);
	ano_ui_rrect(&b, &rect[0], &rect[2], r10, style.rim.rgba, 1.5f, ANO_UI_REF_NONE, ANO_UI_REF_NONE, 0);
	ui_label(&b, bake, anostr_lit("M menu · N music · drag the square"), 20.0f, rect, style.label.rgba,
	         glyphs, &gcount);
	return ano_render_ui_set(bridge, HUD_UI_BAR, 16, &b, glyphs, gcount);
}

/* Submit Policy */

// OOM cooldown for an optional HUD block (500 ms).
#define HUD_OOM_RETRY_US 500000ull

// in:  r, now, dirty (nullable), retryAt (nullable), what
// out: updates *dirty / *retryAt per result
// inv: switch total over AnoRenderSubmitResult (no default)
static void submit_policy(AnoRenderSubmitResult r, uint64_t now, bool* dirty, uint64_t* retryAt,
                          const char* what)
{
	switch (r) {
	case ANO_RENDER_SUBMIT_ACCEPTED:     if (dirty) *dirty = false; if (retryAt) *retryAt = 0; break;
	case ANO_RENDER_SUBMIT_BACKPRESSURE: if (retryAt) *retryAt = 0; break; // retry next tick
	case ANO_RENDER_SUBMIT_OOM:          if (retryAt) *retryAt = now + HUD_OOM_RETRY_US; break;
	case ANO_RENDER_SUBMIT_INVALID:
		if (dirty) *dirty = false; if (retryAt) *retryAt = 0;
		ano_log(ANO_WARN, "HUD: %s block refused as invalid; retired.", what);
		break;
	}
}

// in:  bridge, text_id, inst, shaped, what
// out: ACCEPTED | OOM | INVALID | BACKPRESSURE (shutdown mid-wait only)
// inv: wait observes g_logicShouldStop
static AnoRenderSubmitResult hud_text_spin(AnoRenderBridge* bridge, uint32_t text_id,
                                           AnoGlyphInstance* inst, uint32_t shaped,
                                           const char* what)
{
	for (;;) {
		AnoRenderSubmitResult r = hud_text_submit(bridge, text_id, inst, shaped);
		switch (r) {
		case ANO_RENDER_SUBMIT_ACCEPTED:
			return r;
		case ANO_RENDER_SUBMIT_OOM:
			ano_log(ANO_WARN, "HUD: no memory for the %s block; running without it.", what);
			return r;
		case ANO_RENDER_SUBMIT_INVALID:
			ano_log(ANO_WARN, "HUD: the %s block was refused as invalid; running without it.", what);
			return r;
		case ANO_RENDER_SUBMIT_BACKPRESSURE:
			if (atomic_load(&g_logicShouldStop))
				return r;
			ano_sleep(1000);
			break;
		}
	}
}

/* Logic Thread */

void* anoLogicThreadMain(void* arg)
{
	(void)arg;
	AnoRenderBridge* bridge = anoRenderBridge();

	spawn_scene(bridge);

	// One-shot HUD text (below OSD); spin on backpressure.
	const AnoFontBake* bake = anoRenderTextBake();
	AnoGlyphInstance hud[HUD_TEXT_CAP];
	if (bake != NULL) {
		// Each run's byteCount is sizeof its own segment.
		#define TITLE_HEAD "logic HUD"
		#define TITLE_TAIL " :: text bridge v0"
		const AnoTextRun titleRuns[2] = {
			{ sizeof TITLE_HEAD - 1, 24.0f, { 1.0f, 0.78f, 0.32f, 1.0f } },
			{ sizeof TITLE_TAIL - 1, 24.0f, { 0.9f, 0.9f, 0.9f, 1.0f } },
		};
		const float titleOrg[2] = { 24.0f, 150.0f };
		uint32_t n = ano_text_shape_runs_lit(bake, TITLE_HEAD TITLE_TAIL, titleRuns, 2,
		                                     titleOrg, hud, HUD_TEXT_CAP, NULL);
		if (hud_text_spin(bridge, HUD_TEXT_TITLE, hud, n, "title") == ANO_RENDER_SUBMIT_BACKPRESSURE)
			goto hudDone;
		#undef TITLE_HEAD
		#undef TITLE_TAIL

		const float noticeOrg[2] = { 24.0f, 180.0f };
		const float grey[4] = { 0.6f, 0.6f, 0.6f, 1.0f };
		n = ano_text_shape_lit(bake, "this line clears itself in 15 s",
		                       20.0f, noticeOrg, grey, hud, HUD_TEXT_CAP, NULL);
		if (hud_text_spin(bridge, HUD_TEXT_NOTICE, hud, n, "notice") == ANO_RENDER_SUBMIT_BACKPRESSURE)
			goto hudDone;

		// Unicode sampler: Elder Futhark + Latin-1 + Cyrillic.
		const float samplerOrg[2] = { 24.0f, 240.0f };
		const float gold[4] = { 1.0f, 0.85f, 0.45f, 1.0f };
		n = ano_text_shape_lit(bake,
		                       "ᛖᚲ ᚺᛚᛖᚹᚨᚷᚨᛊᛏᛁᛉ ᚺᛟᛚᛏᛁᛃᚨᛉ ᚺᛟᚱᚾᚨ ᛏᚨᚹᛁᛞᛟ · Руны · æ ß",
		                       22.0f, samplerOrg, gold, hud, HUD_TEXT_CAP, NULL);
		if (hud_text_spin(bridge, HUD_TEXT_UNICODE, hud, n, "unicode sampler") == ANO_RENDER_SUBMIT_BACKPRESSURE)
			goto hudDone;

		// Homer Odyssey 1.1 (polytonic Greek).
		const float homerOrg[2] = { 24.0f, 270.0f };
		const float aegean[4] = { 0.55f, 0.80f, 1.0f, 1.0f };
		n = ano_text_shape_lit(bake,
		                       "Ἄνδρα μοι ἔννεπε, Μοῦσα, πολύτροπον",
		                       22.0f, homerOrg, aegean, hud, HUD_TEXT_CAP, NULL);
		if (hud_text_spin(bridge, HUD_TEXT_HOMER, hud, n, "homer") == ANO_RENDER_SUBMIT_BACKPRESSURE)
			goto hudDone;
	}
// HUD setup done, or abandoned on shutdown.
hudDone:
	uint64_t noticeDeadline = 0; // armed 15s after first frame
	bool     noticeCleared = false;

	// Free-fly: WASD, Space/Ctrl, right-drag look. Seeded eye + pitch so the first publish does not jump.
	float    camEye[3] = { -4.0f, 1.13f, -0.31f };
	float    camYaw = 1.5707963f, camPitch = -0.165f;
	bool     inW = false, inA = false, inS = false, inD = false, inUp = false, inDown = false;
	bool     looking = false, haveCursor = false;
	float    prevCx = 0.0f, prevCy = 0.0f;
	uint64_t lastCam = ano_timestamp_us();
	uint64_t camSeq = 0;
	uint64_t lastSnapLog = ano_timestamp_us();

	// ANO_MENU opens the menu at boot.
	bool     menuVisible = getenv("ANO_MENU") != NULL;
	bool     menuDirty = menuVisible, barSubmitted = false;
	uint64_t menuRetryAt = 0, barRetryAt = 0;
	int      menuHovered = -1;
	uint32_t optionsCount = 0;
	float    vpW = 0.0f, vpH = 0.0f; // last logical viewport from RenderSnapshot
	float    barVpH = 0.0f;

	AnoAudioBridge* ab = anoAudioBridge(); // NULL if MusicGen is off or start failed
	MusicState mus = { .valence = 0.30f, .energy = 0.35f, .tension = 0.20f, .bar = -1 };
	bool musicVisible = false, musicDirty = false, affectDirty = false, flashOn = false;
	uint64_t musicRetryAt = 0;
	int  musicDrag = MUS_DRAG_NONE, musicHovered = MUS_DRAG_NONE;
	uint64_t lastTelem = 0;

	while (!atomic_load(&g_logicShouldStop))
	{
		uint64_t now = ano_timestamp_us();

		// Drain render->logic: input, picking, slot retirement.
		RenderEvent ev;
		while (ano_render_poll_event(bridge, &ev)) {
			switch (ev.kind) {
			case REVENT_INPUT: {
				const AnoInputEvent* ie = &ev.u.input;
				if (ie->kind == ANO_INPUT_KEY) {
					bool down = (ie->u.key.action != GLFW_RELEASE); // PRESS or REPEAT
					switch (ie->u.key.key) {
					case GLFW_KEY_W:            inW = down;    break;
					case GLFW_KEY_S:            inS = down;    break;
					case GLFW_KEY_A:            inA = down;    break;
					case GLFW_KEY_D:            inD = down;    break;
					case GLFW_KEY_SPACE:        inUp = down;   break;
					case GLFW_KEY_LEFT_CONTROL: inDown = down; break;
					case GLFW_KEY_F5:
						if (ie->u.key.action == GLFW_PRESS)
							atomic_store(&g_resourceReloadRequested, true);
						break;
					case GLFW_KEY_F12:
						if (ie->u.key.action == GLFW_PRESS)
							atomic_store(&g_frameCaptureRequested, true);
						break;
					case GLFW_KEY_M:
						if (ie->u.key.action == GLFW_PRESS) {
							menuVisible = !menuVisible;
							menuHovered = -1;
							menuDirty = true;
						}
						break;
					case GLFW_KEY_N:
						if (ie->u.key.action == GLFW_PRESS && ab != NULL) {
							musicVisible = !musicVisible;
							musicHovered = MUS_DRAG_NONE;
							musicDrag = MUS_DRAG_NONE;
							musicDirty = true;
						}
						break;
					default: break;
					}
				} else if (ie->kind == ANO_INPUT_MOUSE_BUTTON) {
					if (ie->u.button.button == GLFW_MOUSE_BUTTON_RIGHT)
						looking = (ie->u.button.action == GLFW_PRESS);
					else if (ie->u.button.button == GLFW_MOUSE_BUTTON_LEFT
					         && ie->u.button.action == GLFW_RELEASE) {
						musicDrag = MUS_DRAG_NONE;
					}
					if (ie->u.button.button == GLFW_MOUSE_BUTTON_LEFT
					    && ie->u.button.action == GLFW_PRESS
					    && musicVisible && vpW > 0.0f) {
						MusicLayout ml;
						music_layout(vpW, vpH, &ml);
						musicDrag = music_hit(&ml, prevCx, prevCy);
						if (musicDrag != MUS_DRAG_NONE) {
							music_drag_apply(&ml, musicDrag, prevCx, prevCy, &mus);
							affectDirty = musicDirty = true;
						}
					}
					if (ie->u.button.button == GLFW_MOUSE_BUTTON_LEFT
					         && ie->u.button.action == GLFW_PRESS
					         && menuVisible && vpW > 0.0f) {
						MenuLayout ml;
						menu_layout(vpW, vpH, &ml);
						switch (menu_hit(&ml, prevCx, prevCy)) {
						case 0: menuVisible = false; menuDirty = true; break;   // RESUME
						case 1: optionsCount++;      menuDirty = true; break;   // OPTIONS
						case 2:                                                  // QUIT
							menuVisible = false;
							menuDirty = true;
							ano_log(ANO_INFO, "Menu: quit selected (demo no-op).");
							break;
						default: break;
						}
					}
				} else if (ie->kind == ANO_INPUT_CURSOR_POS) {
					float cx = ie->u.cursor.x, cy = ie->u.cursor.y;
					if (musicDrag != MUS_DRAG_NONE && vpW > 0.0f) {
						MusicLayout ml;
						music_layout(vpW, vpH, &ml);
						music_drag_apply(&ml, musicDrag, cx, cy, &mus);
						affectDirty = musicDirty = true; // coalesced: one command per tick
					}
					if (looking && haveCursor) {
						camYaw   += (cx - prevCx) * 0.003f;
						camPitch -= (cy - prevCy) * 0.003f;
						if (camPitch >  1.5f) camPitch =  1.5f;
						if (camPitch < -1.5f) camPitch = -1.5f;
					}
					prevCx = cx; prevCy = cy; haveCursor = true;
				}
				break;
			}
			case REVENT_PICK_RESULT:
				if (ev.u.pick_render_id != ANO_RENDER_NO_PICK)
					ano_debug_log(ANO_INFO, "Pick: cursor over render_id %u", ev.u.pick_render_id);
				break;
			case REVENT_SLOT_RETIRED:   break; // render_id free to recycle (ignored)
			case REVENT_BATCH_CONSUMED: break; // batch ack (ignored)
			case REVENT_CAPACITY:
				ano_log(ANO_WARN, "Producer: back-channel saturated; some input samples were dropped.");
				break;
			}
		}

		{
			float dt = (now - lastCam) / 1000000.0f; lastCam = now;
			if (dt > 0.1f) dt = 0.1f; // clamp hitch
			float cp = cosf(camPitch), sp = sinf(camPitch), sy = sinf(camYaw), cy = cosf(camYaw);
			float fwd[3]   = { cp * sy, sp, -cp * cy }; // RH, looks down -Z at yaw 0
			float right[3] = { cy, 0.0f, sy };          // normalize(cross(fwd, worldUp))
			float step = 2.5f * dt;                     // units/sec
			float mF = (float)((int)inW - (int)inS);
			float mR = (float)((int)inD - (int)inA);
			float mU = (float)((int)inUp - (int)inDown);
			for (int k = 0; k < 3; k++)
				camEye[k] += (fwd[k] * mF + right[k] * mR) * step;
			camEye[1] += mU * step;

			AnoViewState view = { .fovYDeg = 45.0f, .seq = ++camSeq };
			for (int k = 0; k < 3; k++) {
				view.eye[k]    = camEye[k];
				view.center[k] = camEye[k] + fwd[k];
				view.up[k]     = 0.0f;
			}
			view.up[1] = 1.0f;
			ano_render_publish_view(bridge, &view);
		}

		// ACCEPTED retires the notice; otherwise retry next tick.
		if (bake != NULL && !noticeCleared && noticeDeadline != 0 && now > noticeDeadline)
			noticeCleared = ano_render_text_clear(bridge, HUD_TEXT_NOTICE) == ANO_RENDER_SUBMIT_ACCEPTED;

		// Hover change dirties; a full ring keeps dirty.
		if (menuVisible && vpW > 0.0f) {
			MenuLayout ml;
			menu_layout(vpW, vpH, &ml);
			int h = menu_hit(&ml, prevCx, prevCy);
			if (h != menuHovered) {
				menuHovered = h;
				menuDirty = true;
			}
		}
		// OOM cooldown gates layout + submit.
		if (menuDirty && vpW > 0.0f && now >= menuRetryAt) {
			MenuLayout ml;
			menu_layout(vpW, vpH, &ml);
			submit_policy(submit_menu(bridge, bake, &ml, menuVisible, menuHovered, optionsCount),
			              now, &menuDirty, &menuRetryAt, "menu");
		}

		if (ab != NULL) {
			// One ACMD_MUSIC_AFFECT per tick; urgent = next barline.
			if (affectDirty) {
				AnoAudioCommand c = { .kind = ACMD_MUSIC_AFFECT,
				                      .affect = { mus.valence, mus.energy, mus.tension },
				                      .urgent = true };
				if (ano_audio_submit(ab, &c))
					affectDirty = false; // else: ring full, try again next tick
			}

			// AEVT_MUSIC_BAR on the audible downbeat (composed two bars ahead).
			AnoAudioEvent aev;
			while (ano_audio_poll_event(ab, &aev)) {
				if (aev.kind != AEVT_MUSIC_BAR)
					continue;
				mus.bar = aev.u.music.bar;
				mus.keyTonic = aev.u.music.keyTonic;
				mus.mode = aev.u.music.mode;
				mus.chordDegree = aev.u.music.chordDegree;
				mus.isCadence = aev.u.music.isCadence;
				if (aev.u.music.isCadence) {
					mus.flashUntil = now + 500000ull; // 0.5 s
					flashOn = true;
				}
				musicDirty = musicVisible;
			}
			if (flashOn && now >= mus.flashUntil) { // one last flash frame
				flashOn = false;
				musicDirty = musicVisible;
			}
			if (musicVisible && now - lastTelem > 500000ull) {
				AnoAudioTelemetry t;
				if (ano_audio_acquire_telemetry(ab, &t) && t.genUs != mus.genUs) {
					mus.genUs = t.genUs;
					musicDirty = true;
				}
				lastTelem = now;
			}
		}

		if (musicVisible && vpW > 0.0f) {
			MusicLayout ml;
			music_layout(vpW, vpH, &ml);
			int h = musicDrag != MUS_DRAG_NONE ? musicDrag : music_hit(&ml, prevCx, prevCy);
			if (h != musicHovered) {
				musicHovered = h;
				musicDirty = true;
			}
		}
		// OOM cooldown gates layout + submit.
		if (musicDirty && vpW > 0.0f && now >= musicRetryAt) {
			MusicLayout ml;
			music_layout(vpW, vpH, &ml);
			submit_policy(submit_music(bridge, bake, &ml, musicVisible, &mus, musicHovered, now),
			              now, &musicDirty, &musicRetryAt, "music panel");
		}

		// ~1 Hz snapshot log and camera readout.
		{
			RenderSnapshot snap;
			if (noticeDeadline == 0 && ano_render_acquire_snapshot(bridge, &snap))
				noticeDeadline = now + 15000000ull; // first published frame: arm the notice
			if (ano_render_acquire_snapshot(bridge, &snap)) {
				if (menuVisible && (vpW != snap.uiWidth || vpH != snap.uiHeight))
					menuDirty = true; // layout is centered
				if (musicVisible && (vpW != snap.uiWidth || vpH != snap.uiHeight))
					musicDirty = true; // right-anchored: resize moves the panel
				vpW = snap.uiWidth;
				vpH = snap.uiHeight;
			}
			if ((!barSubmitted || barVpH != vpH) && vpH > 0.0f && now >= barRetryAt) {
				bool barDirty = true;
				submit_policy(submit_bar(bridge, bake, vpH), now, &barDirty, &barRetryAt, "status bar");
				barSubmitted = !barDirty;
				if (barSubmitted)
					barVpH = vpH;
			}
			if (ano_render_acquire_snapshot(bridge, &snap) && now - lastSnapLog > 1000000) {
				ano_debug_log(ANO_INFO, "Snapshot: frameId %llu, viewport %ux%u",
				       (unsigned long long)snap.frameId, snap.vpWidth, snap.vpHeight);
				lastSnapLog = now;
				if (bake != NULL) {
					char cam[96];
					int len = snprintf(cam, sizeof cam, "cam  x %+.2f  y %+.2f  z %+.2f",
					                   (double)camEye[0], (double)camEye[1], (double)camEye[2]);
					if (len > 0) {
						const float camOrg[2] = { 24.0f, 210.0f };
						const float mint[4] = { 0.45f, 0.95f, 0.6f, 1.0f };
						uint32_t n = ano_text_shape(bake, anostr_view(cam, (size_t)len),
						                            20.0f, camOrg, mint, hud, HUD_TEXT_CAP, NULL);
						// 1 s refresh is the retry; no dirty/cooldown.
						submit_policy(hud_text_submit(bridge, HUD_TEXT_CAM, hud, n), now, NULL, NULL,
						              "camera readout");
					}
				}
			}
		}
		ano_sleep(2000); // ~2 ms logic tick
	}
	return NULL;
}
#endif // !HEADLESS_BUILD

/* Main */

int main()
{
    // Bindings and shaders are game-relative; the executable directory is the filesystem root.
    if (!ano_fs_chdir_gamepath())
        ano_rlog(ANO_WARN, ANO_TERM | ANO_NOW, "Warning: could not set the working directory to the executable's; "
               "assets will load relative to the current working directory.");

    #ifdef DEBUG_BUILD

    mi_option_enable(mi_option_show_errors);
    mi_option_enable(mi_option_show_stats);
    mi_option_enable(mi_option_verbose);
    ano_debug_rlog(ANO_INFO, ANO_TERM | ANO_NOW, "Running in debug mode!");

    #endif

    // Process-wide logger; ANO_LOG_SCOPE_ATTR cleans on scope exit.
    int logAlive ANO_LOG_SCOPE_ATTR = ano_log_init();
    if (logAlive != 0) {
        ano_log(ANO_FATAL, "Logger initialization failed; something is very wrong.");
        return EXIT_FAILURE;
    }

    // Blackbox: fatal signal -> CRASH log + flush.
    if (ano_log_crash_init() != 0)
        ano_log(ANO_WARN, "Blackbox failed to arm; a crash will leave no CRASH log.");

    size_t mainStack = ano_thread_main_stack();
    if (mainStack != 0 && mainStack < ANO_THREAD_STACK_SIZE)
        ano_log(ANO_WARN, "Main-thread stack budget is %zu KiB, under the engine's %zu KiB: "
                "deep main-thread call chains may overflow (raise `ulimit -s`).",
                mainStack >> 10, (size_t)ANO_THREAD_STACK_SIZE >> 10);

#ifndef HEADLESS_BUILD
    StartupResources startup{};
    const AnoResourceError startupResult = create_startup_resources(&startup);
    AnoResourceManager *resources = startup.manager;
    if (startupResult != ANO_RESOURCE_OK) {
        ano_log(ANO_FATAL, "Resource initialization failed: %s",
                ano_resource_error_string(startupResult));
        return -1;
    }
    // GLFW + Vulkan on main (window/events pinned; mandatory on macOS).
    // initVulkan creates the bridge before the producer; no readiness handshake.
    if (!initVulkan(resources))
    {
        ano_log(ANO_FATAL, "Vulkan initialization failed.");
        ano_resource_manager_destroy(resources);
        ano_resource_cooker_destroy(startup.cooker);
        return -1;
    }

#if defined(ANOPTIC_ENGINE_MUSIC)
    // Audio world before the producer. false: silent run.
    if (!music_world_start())
        ano_log(ANO_WARN, "Music: the audio world did not come up; running silent.");
#endif

    // Sole render-command producer.
    anothread_t logicThread;
    if (ano_thread_create(&logicThread, NULL, anoLogicThreadMain, NULL) != 0)
    {
        ano_log(ANO_FATAL, "Failed to spawn logic thread.");
#if defined(ANOPTIC_ENGINE_MUSIC)
        music_world_stop(true);
#endif
        unInitVulkan();
        ano_resource_manager_destroy(resources);
        ano_resource_cooker_destroy(startup.cooker);
        return -1;
    }

    // Poll + draw on main. Logic publishes concurrently.
    bool replacementSourceActive = false;
    ReloadWorker reloadWorker = {
        .manager = resources,
        .cooker = startup.cooker,
    };
    AnoRenderResourcePublication *publication = nullptr;
    if (!reload_worker_start(reloadWorker)) {
        ano_log(ANO_FATAL, "Resource reload worker did not start.");
        atomic_store(&g_logicShouldStop, true);
        ano_thread_join(logicThread, NULL);
        unInitVulkan();
        ano_resource_manager_destroy(resources);
        ano_resource_cooker_destroy(startup.cooker);
        return -1;
    }
    while (!anoShouldClose())
    {
        glfwPollEvents();
        if (atomic_exchange(&g_frameCaptureRequested, false)) {
            const char *path = getenv("ANO_FRAME_CAPTURE_PATH");
            if (path == nullptr || path[0] == '\0')
                path = "anoptic-frame.ppm";
            if (!ano_render_capture_next_frame(path))
                ano_log(ANO_WARN, "Frame capture request was refused.");
        }
        const bool reloadRequested = atomic_exchange(
            &g_resourceReloadRequested, false);
        if (reloadRequested && publication != nullptr) {
            ano_render_resources_cancel_reload(publication);
            publication = nullptr;
            reload_worker_reclaim(reloadWorker);
        }
        if (reloadRequested) {
            StartupSources selected = DEFAULT_SOURCES;
            const char *replacement = getenv("ANO_VIKING_RELOAD_SOURCE");
            if (replacement != nullptr && replacement[0] != '\0'
                && !replacementSourceActive)
                selected.viking = replacement;
            if (!reload_worker_request(reloadWorker, selected))
                ano_log(ANO_ERROR, "Resource reload worker did not start.");
        }
        if (publication != nullptr) {
            const AnoRenderResourceReloadStatus status =
                ano_render_resources_poll_reload(&publication);
            if (status != ANO_RENDER_RESOURCE_RELOAD_PENDING) {
                if (status == ANO_RENDER_RESOURCE_RELOAD_COMMITTED) {
                    const char *replacement = getenv(
                        "ANO_VIKING_RELOAD_SOURCE");
                    if (replacement != nullptr && replacement[0] != '\0')
                        replacementSourceActive = !replacementSourceActive;
                    ano_log(ANO_INFO, "Resource epoch reload committed.");
                } else
                    ano_log(ANO_ERROR, "Resource epoch reload rejected.");
                reload_worker_reclaim(reloadWorker);
            }
        }
        AnoResourceError reloaded = ANO_RESOURCE_OK;
        if (reload_worker_take(
                reloadWorker, &reloaded, &publication)) {
            if (reloaded != ANO_RESOURCE_OK)
                ano_log(ANO_ERROR, "Resource epoch reload rejected: %s",
                        ano_resource_error_string(reloaded));
            if (publication == nullptr)
                reload_worker_reclaim(reloadWorker);
        }
        drawFrame();
    }

    ano_render_resources_cancel_reload(publication);
    if (publication) reload_worker_reclaim(reloadWorker);
    reload_worker_stop(reloadWorker);

    // Stop the producer first and join. No submit may race bridge destruction in unInitVulkan().
    atomic_store(&g_logicShouldStop, true);
    ano_thread_join(logicThread, NULL);

    // Audio teardown after the producer joins.
#if defined(ANOPTIC_ENGINE_MUSIC)
    music_world_stop(true);
#endif

    unInitVulkan();
    ano_resource_manager_destroy(resources);
    ano_resource_cooker_destroy(startup.cooker);
#else
    // Headless engine: no renderer. Idle console loop.
    ano_rlog(ANO_INFO, ANO_TERM, "Anoptic Engine 〜 headless console mode.");
    while (true) {
        ano_rlog(ANO_INFO, ANO_TERM, "Waiting...");
        ano_sleep(3 * 1000000);
    };
#endif

    return 0;
}
