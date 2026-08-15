/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0
 * Anoptic targets ISO C++26. */
/*  == Anoptic Game Engine v0.0000001 == */

/**
 * @file anoptic_render.h
 * @brief Public engine<->renderer contract: lifecycle + logic->render commands.
 *
 * Sole renderer header for the engine entry point. Render world (Vulkan + GLFW) on main thread;
 * logic/ECS is the sole command producer via opaque AnoRenderBridge.
 * Transport (SPSC rings, events, snapshot, viewstate) stays private in src/render_bridge/.
 */

#pragma once

#include <stdint.h>
#include <anoptic_math.h>
#include <anoptic_results.h>
#include <anoptic_text.h> // logic-side shaping
#include <anoptic_ui.h>   // logic-side layout
#include <anoptic_resources_runtime.h>

namespace ano {

enum class RenderError : uint8_t {
    unavailable, backpressure, capacity, out_of_memory, invalid_argument,
};

template<class Value = void>
using RenderResult = Result<Value, RenderError>;

// ---------------------------------------------------------------------------
// Renderer lifecycle (render world; runs on the main thread)
// ---------------------------------------------------------------------------
// GLFW pins window + events to main thread (macOS-mandatory).

// Window, device, assets.
[[nodiscard]] RenderResult<> initVulkan(AnoResourceManager *resources);

// Tear down the render world and the bridge.
void unInitVulkan(void);

// One frame; drains pending commands.
void drawFrame(void);

// Copy the next fully composited swapchain image to a binary PPM at path.
// The path is copied.
[[nodiscard]] RenderResult<> render_capture_next_frame(const char *path);

// true once the window has been asked to close.
bool anoShouldClose(void);

// ---------------------------------------------------------------------------
// Bridge handle
// ---------------------------------------------------------------------------

// Opaque logic<->render channel. Created in initVulkan(); destroyed in unInitVulkan().
struct AnoRenderBridge;

// Producer endpoint. Available once initVulkan() has returned.
[[nodiscard]] RenderResult<AnoRenderBridge *> anoRenderBridge(void);

// Resident scene queries. Logic composes instances from stable asset handles.

// One spawnable primitive: GPU mesh + material + world transform (node-local under caller's root).
struct AnoRenderableDesc {
    mat4     transform;
    uint32_t mesh_index;
    uint32_t material_index;
    AnoAssetId resource_asset;
    uint32_t resource_primitive;
};

// Total primitive count; fills out[0..min(count, cap)).
[[nodiscard]] RenderResult<uint32_t> anoRenderAssetPrimitives(
    AnoAssetId asset, const mat4 root, AnoRenderableDesc *out, uint32_t cap);

// Fallback cube mesh index + default material for procedural renderables.
uint32_t anoRenderFallbackMesh(void);
uint32_t anoRenderDefaultMaterial(void);

// Baked font for logic-side shaping (text_shape/_runs). Immutable; any thread.
// Ship instances via render_text_set. Available after successful renderer text init.
[[nodiscard]] RenderResult<const AnoFontBake *> anoRenderTextBake(void);

// Rows [0, anoRenderStaticLightBase()) are STATIC scene lights (RCMD_CREATE + light_index).
// Rows above: runtime attach registry. Scene light_index MUST be < this.
uint32_t anoRenderStaticLightBase(void);

// ---------------------------------------------------------------------------
// Command protocol: logic -> render
// ---------------------------------------------------------------------------

// Absent-attribute sentinels. NO_MESH = no geometry (cull skips; e.g. pure light).
#define ANO_RENDER_NO_MESH  0xFFFFFFFFu
#define ANO_RENDER_NO_LIGHT 0xFFFFFFFFu

// Geometry pool index of the fallback cube; assigned first at upload time.
#define FALLBACK_MESH_INDEX 0

// glTF KHR_lights_punctual. Photometrics only; world pos/dir from the driving renderable's transform.
enum RenderLightType {
    RENDER_LIGHT_DIRECTIONAL = 0,
    RENDER_LIGHT_POINT       = 1,
    RENDER_LIGHT_SPOT        = 2,
};

struct RenderLightParams {
    float           color[3];     // linear RGB; intensity carries magnitude
    float           intensity;    // candela-like (point/spot) or lux-like (directional)
    float           range;        // cutoff; <= 0 unbounded (ignored for directional)
    float           innerConeCos; // spot inner half-angle cosine
    float           outerConeCos; // spot outer half-angle cosine
    // Outside {0,1,2}: CREATE/UPDATE drop the light payload; LIGHT_ATTACH dropped; LIGHT_UPDATE only if the mask names TYPE.
    RenderLightType type;
    // Spot/dir aim in parent MODEL space. World forward = rotate(parent, localDir).
    // (0,0,0) -> parent -Z. Point: ignored.
    float           localDir[3];
    // 1 allocates a shadow frustum (silent if the budget is full). dir/spot = 1, point = 6.
    // Toggle via render_light_update_fields + ANO_LIGHT_FIELD_CAST.
    // STATIC row: CREATE-only grant; UPDATE 1 refreshes owned volumes; UPDATE 0 revokes.
    uint32_t        castsShadow;
};

struct AnoSceneLightDesc {
    mat4 transform;
    RenderLightParams light;
};

// Flattens canonical scene lights below caller root. Returns the total count.
[[nodiscard]] RenderResult<uint32_t> anoRenderAssetLights(
    AnoAssetId asset, const mat4 root, AnoSceneLightDesc *out, uint32_t cap);

// Field mask for render_light_update_fields. Unnamed fields preserved. ALL = full overwrite.
enum {
    ANO_LIGHT_FIELD_COLOR     = 1 << 0,
    ANO_LIGHT_FIELD_INTENSITY = 1 << 1,
    ANO_LIGHT_FIELD_RANGE     = 1 << 2,
    ANO_LIGHT_FIELD_CONE      = 1 << 3, // innerConeCos + outerConeCos
    ANO_LIGHT_FIELD_TYPE      = 1 << 4,
    ANO_LIGHT_FIELD_OFFSET    = 1 << 5, // light_offset[3]
    ANO_LIGHT_FIELD_DIRECTION = 1 << 6, // localDir[3]
    ANO_LIGHT_FIELD_ALL       = (1 << 7) - 1, // bits 0..6; preserves cast
    // CAST is outside ALL: frustum alloc/free only on explicit request.
    ANO_LIGHT_FIELD_CAST      = 1 << 7,
};

// Occlusion model: shadow maps vs radiance cascades per light type. Default = all shadow-mapped. Toggle: L key.
enum AnoLightingMode {
    ANO_LIGHTING_SHADOWMAP  = 0, // all sources shadow-mapped (default)
    ANO_LIGHTING_HYBRID     = 1, // radiance cascades for points; shadow maps for directional + spot
    ANO_LIGHTING_RC         = 2, // all sources via radiance cascades (no shadow maps)
    ANO_LIGHTING_MODE_COUNT = 3,
};

// Continuous GPU motion. Establish once via RFIELD_ANIM; GPU derives transform from global time.
// Discrete trajectory change re-sends. CPU/physics motion uses ANO_MOTION_STREAMED.
enum AnoMotionType {
    ANO_MOTION_STATIC = 0, // no motion; live transform == base pose
    ANO_MOTION_SPIN,       // constant-rate rotation in the body's local frame (base * R)
    ANO_MOTION_ORBIT,      // constant-rate revolution about a world axis    (R * base)
    ANO_MOTION_LINEAR,     // constant-velocity translation from the base pose
    ANO_MOTION_KEPLER,     // closed-form elliptical orbit; base-pose origin is the focus
    ANO_MOTION_STREAMED,   // CPU-driven; transform arrives per-tick via RCMD_STREAM_TRANSFORMS
};

// Per-renderable motion params. 48 bytes; matches std430
// { uint type; uint flags; float epoch; float pad; vec4 p0; vec4 p1; }.
// epoch = t0; GPU evaluates (time - epoch). p0/p1 hold type params:
//   SPIN / ORBIT : p0.xyz = axis * angular_speed (rad/s)
//   LINEAR       : p0.xyz = velocity (units/s)
//   KEPLER       : p0 = (semiMajorAxis, eccentricity, inclination, longAscendingNode)
//                  p1 = (argPeriapsis, meanAnomalyAtEpoch, meanMotion, _) [rad]
struct AnoMotionDescriptor {
    uint32_t type;
    uint32_t flags;  // reserved
    float    epoch;
    float    _pad;   // aligns p0 to a 16-byte boundary
    Vector4  p0;
    Vector4  p1;
}; // 48 bytes

enum class AnoRenderCommandPayload : uint8_t { create, update, destroy, bulk_create, bulk_update, bulk_destroy, stream, light_attach, light_update, light_detach, text_set, text_clear, ui_set, ui_clear };
enum class AnoRenderPayloadOwnership : uint8_t { inline_value, conditional_owned };
enum class AnoRenderLightPolicy : uint8_t { none, entity, attach, update };
struct AnoRenderCommandContract final { AnoRenderCommandPayload payload; AnoRenderPayloadOwnership ownership; AnoRenderLightPolicy lightPolicy; };

enum RenderCommandKind {
    RCMD_CREATE [[=AnoRenderCommandContract{AnoRenderCommandPayload::create, AnoRenderPayloadOwnership::inline_value, AnoRenderLightPolicy::entity}]],
    RCMD_UPDATE [[=AnoRenderCommandContract{AnoRenderCommandPayload::update, AnoRenderPayloadOwnership::inline_value, AnoRenderLightPolicy::entity}]],
    RCMD_DESTROY [[=AnoRenderCommandContract{AnoRenderCommandPayload::destroy, AnoRenderPayloadOwnership::inline_value, AnoRenderLightPolicy::none}]],
    RCMD_BULK_CREATE [[=AnoRenderCommandContract{AnoRenderCommandPayload::bulk_create, AnoRenderPayloadOwnership::conditional_owned, AnoRenderLightPolicy::none}]],
    RCMD_BULK_UPDATE [[=AnoRenderCommandContract{AnoRenderCommandPayload::bulk_update, AnoRenderPayloadOwnership::conditional_owned, AnoRenderLightPolicy::none}]],
    RCMD_BULK_DESTROY [[=AnoRenderCommandContract{AnoRenderCommandPayload::bulk_destroy, AnoRenderPayloadOwnership::conditional_owned, AnoRenderLightPolicy::none}]],
    RCMD_STREAM_TRANSFORMS [[=AnoRenderCommandContract{AnoRenderCommandPayload::stream, AnoRenderPayloadOwnership::inline_value, AnoRenderLightPolicy::none}]],
    RCMD_LIGHT_ATTACH [[=AnoRenderCommandContract{AnoRenderCommandPayload::light_attach, AnoRenderPayloadOwnership::inline_value, AnoRenderLightPolicy::attach}]],
    RCMD_LIGHT_UPDATE [[=AnoRenderCommandContract{AnoRenderCommandPayload::light_update, AnoRenderPayloadOwnership::inline_value, AnoRenderLightPolicy::update}]],
    RCMD_LIGHT_DETACH [[=AnoRenderCommandContract{AnoRenderCommandPayload::light_detach, AnoRenderPayloadOwnership::inline_value, AnoRenderLightPolicy::none}]],
    RCMD_TEXT_SET [[=AnoRenderCommandContract{AnoRenderCommandPayload::text_set, AnoRenderPayloadOwnership::conditional_owned, AnoRenderLightPolicy::none}]],
    RCMD_TEXT_CLEAR [[=AnoRenderCommandContract{AnoRenderCommandPayload::text_clear, AnoRenderPayloadOwnership::inline_value, AnoRenderLightPolicy::none}]],
    RCMD_UI_SET [[=AnoRenderCommandContract{AnoRenderCommandPayload::ui_set, AnoRenderPayloadOwnership::conditional_owned, AnoRenderLightPolicy::none}]],
    RCMD_UI_CLEAR [[=AnoRenderCommandContract{AnoRenderCommandPayload::ui_clear, AnoRenderPayloadOwnership::inline_value, AnoRenderLightPolicy::none}]],
};

// CREATE/UPDATE payload bits. Multiple bits = multi-field update in one message.
enum RenderFieldBits {
    RFIELD_TRANSFORM = 1 << 0, // teleport: rewrite the BASE pose (initialTransform), never the GPU-output transform
    RFIELD_MESH_MAT  = 1 << 1, // mesh and/or material index
    RFIELD_ANIM      = 1 << 2, // establishes or changes continuous GPU motion
    RFIELD_LIGHT     = 1 << 3,
    RFIELD_USERDATA  = 1 << 4, // AnoInstanceData
};

struct AnoRenderFieldUse final { uint32_t fields; uint32_t commands; };
struct AnoRenderBulkRequired final {};
struct AnoRenderOwnedPayloadFor final { uint32_t commands; };

// Per-renderable instance channel. Game owns pack/unpack.
//
// Convention:
//   packed[0] : RGBA8 tint               (GLSL unpackUnorm4x8)
//   packed[1] : flag bits                 (bit 0 = tint enabled; rest reserved)
//   packed[2] : reserved (e.g. two fp16 scalars / two u16 texture-layer indices)
//   packed[3] : reserved
//   params    : reserved full-precision scalars (e.g. anim phase, build progress)
// All-zero = inert (renderer ignores until the game opts in).
struct AnoInstanceData {
    uint32_t packed[4];
    Vector4  params;
}; // 32 bytes; matches std430 { uvec4 packed; vec4 params; }

// Initial-state batch for RCMD_BULK_CREATE. No public copy helper: producer owns the arrays unless bulk_owned.
// UPDATE/DESTROY helpers copy at submit; render frees the copy.
struct RenderCreateBatch {
    uint32_t        count;
    const uint32_t *render_ids;  // [count] logical names
    const mat4     *transforms;  // [count] base poses
    const AnoMotionDescriptor *motion; // [count]; ANO_MOTION_STATIC for none
    const uint32_t *mesh;        // [count]; ANO_RENDER_NO_MESH allowed
    const uint32_t *material;    // [count]
};

// Mass field change (RCMD_BULK_UPDATE): one shared `fields` mask across a render_id array.
// Only flagged arrays are read (rest may be NULL). RFIELD_LIGHT is not bulk.
// Submit via render_submit_bulk_update (copies; caller arrays live until return).
struct RenderUpdateBatch {
    uint32_t        count;
    uint32_t        fields;       // RenderFieldBits shared by every entry
    const uint32_t *render_ids [[=AnoRenderBulkRequired{}]]; // [count]; unresolved ids are skipped
    const mat4 *transforms [[=AnoRenderFieldUse{RFIELD_TRANSFORM, 1u << RCMD_BULK_UPDATE}]]; // teleport rewrites base pose
    const AnoMotionDescriptor *motion [[=AnoRenderFieldUse{RFIELD_ANIM, 1u << RCMD_BULK_UPDATE}]];
    const uint32_t *mesh [[=AnoRenderFieldUse{RFIELD_MESH_MAT, 1u << RCMD_BULK_UPDATE}]];
    const uint32_t *material [[=AnoRenderFieldUse{RFIELD_MESH_MAT, 1u << RCMD_BULK_UPDATE}]];
    const AnoInstanceData *instance_data [[=AnoRenderFieldUse{RFIELD_USERDATA, 1u << RCMD_BULK_UPDATE}]];
};

// Mass despawn (RCMD_BULK_DESTROY). Submit via render_submit_bulk_destroy (copies until return).
struct RenderDestroyBatch {
    uint32_t        count;
    const uint32_t *render_ids;  // [count]; unresolved ids are skipped
};

// Screen-text region capacity. One block never exceeds it; union of live blocks truncates in block order.
#define ANO_RENDER_TEXT_MAX 8192u

// Screen-text block (RCMD_TEXT_SET): shaped glyphs, addressed by producer text_id.
// SET replaces; CLEAR removes. Shape against anoRenderTextBake(); origins/sizes in overlay logical units.
// Submit via render_text_set (copies until return).
struct RenderTextBlock {
    uint32_t                count;
    const AnoGlyphInstance *instances;  // [count] shaped glyphs (48-byte GPU ABI)
};

// Per-block caps for RCMD_UI_SET. Union overflow skips the whole block (never truncates).
#define ANO_RENDER_UI_MAX_PRIMS  1024u
#define ANO_RENDER_UI_MAX_CLIPS  64u
#define ANO_RENDER_UI_MAX_PAINTS 64u
#define ANO_RENDER_UI_MAX_STOPS  256u
#define ANO_RENDER_UI_MAX_CURVES 8192u // packed path curve words per block
#define ANO_RENDER_UI_MAX_GLYPHS 2048u

// UI surface id. Block coords are logical units of that surface; fold at compose (docs/ui/ui-render.md §3.11).
// Overlay only (content scale; extent = RenderSnapshot.uiWidth/uiHeight).
#define ANO_UI_SURFACE_OVERLAY 0u

// UI block (RCMD_UI_SET): z-ordered prims + tables + glyphs, addressed by producer ui_id.
// SET replaces; CLEAR removes. Compose by (layer, creation order); prim index = paint order.
// Refs are block-local; rebased at compose. scroll adds to positions before surface fold (0 today).
// Submit via render_ui_set.
struct RenderUiBlock {
    uint32_t layer;
    uint32_t surface;  // ANO_UI_SURFACE_*; overlay only today
    float    scroll[2];
    uint32_t primCount;
    uint32_t clipCount;
    uint32_t paintCount;
    uint32_t stopCount;
    uint32_t curveCount; // packed path curve words in curves[]
    uint32_t glyphCount;
    const AnoUiPrim        *prims;
    const AnoUiClip        *clips;
    const AnoUiPaint       *paints;
    const AnoUiStop        *stops;
    const uint32_t         *curves;
    const AnoGlyphInstance *glyphs;
};

// Zero-copy streamed-transform write region.
// begin reserves a GPU ring slice; write ids/xforms; commit publishes RCMD_STREAM_TRANSFORMS.
// Valid between successful begin and commit; single-producer.
struct AnoStreamRegion {
    uint32_t *ids;       // [capacity] streamed render_ids
    mat4     *xforms;    // [capacity] live world transforms (initialTransform space)
    uint32_t  capacity;  // entries this slice holds
    uint64_t  token;     // opaque slice identity; pass back to render_stream_commit
};

// POD, fixed-size, copied by value through the ring. Fat (mat4) but CREATE needs it;
// UPDATE only reads fields flagged in `fields`.
struct RenderCommand {
    RenderCommandKind kind;
    uint32_t          render_id;        // logical name; valid for CREATE/UPDATE/DESTROY
    uint32_t          fields;           // RenderFieldBits, for CREATE/UPDATE

    mat4 transform [[=AnoRenderFieldUse{RFIELD_TRANSFORM, (1u << RCMD_CREATE) | (1u << RCMD_UPDATE)}]]; // base pose
    AnoMotionDescriptor motion [[=AnoRenderFieldUse{RFIELD_ANIM, (1u << RCMD_CREATE) | (1u << RCMD_UPDATE)}]];
    uint32_t mesh_index [[=AnoRenderFieldUse{RFIELD_MESH_MAT, (1u << RCMD_CREATE) | (1u << RCMD_UPDATE)}]];
    uint32_t material_index [[=AnoRenderFieldUse{RFIELD_MESH_MAT, (1u << RCMD_CREATE) | (1u << RCMD_UPDATE)}]];
    AnoAssetId resource_asset [[=AnoRenderFieldUse{RFIELD_MESH_MAT, (1u << RCMD_CREATE) | (1u << RCMD_UPDATE)}]];
    uint32_t resource_primitive [[=AnoRenderFieldUse{RFIELD_MESH_MAT, (1u << RCMD_CREATE) | (1u << RCMD_UPDATE)}]];
    mat4 resource_root [[=AnoRenderFieldUse{RFIELD_MESH_MAT, (1u << RCMD_CREATE) | (1u << RCMD_UPDATE)}]];
    uint32_t          light_index;      // ANO_RENDER_NO_LIGHT if not a light
    RenderLightParams light [[=AnoRenderFieldUse{RFIELD_LIGHT, (1u << RCMD_CREATE) | (1u << RCMD_UPDATE)}]]; // also LIGHT_ATTACH/UPDATE
    uint32_t          light_id;         // RCMD_LIGHT_* : producer-owned logical light handle
    float             light_offset[3];  // RCMD_LIGHT_ATTACH/UPDATE : offset in the parent's model space
    uint32_t          light_fields;     // RCMD_LIGHT_UPDATE : ANO_LIGHT_FIELD_* mask (0 == ALL)
    AnoInstanceData instance_data [[=AnoRenderFieldUse{RFIELD_USERDATA, (1u << RCMD_CREATE) | (1u << RCMD_UPDATE)}]];

    const RenderCreateBatch *batch [[=AnoRenderOwnedPayloadFor{1u << RCMD_BULK_CREATE}]];
    const RenderUpdateBatch *update [[=AnoRenderOwnedPayloadFor{1u << RCMD_BULK_UPDATE}]];
    const RenderDestroyBatch *destroy [[=AnoRenderOwnedPayloadFor{1u << RCMD_BULK_DESTROY}]];
    const RenderTextBlock *text [[=AnoRenderOwnedPayloadFor{1u << RCMD_TEXT_SET}]]; // render-owned copy; registry adopts it
    uint32_t          text_id;          // RCMD_TEXT_SET/CLEAR : producer-owned logical block handle
    const RenderUiBlock *ui [[=AnoRenderOwnedPayloadFor{1u << RCMD_UI_SET}]]; // render-owned copy; registry adopts it
    uint32_t          ui_id;            // RCMD_UI_SET/CLEAR : producer-owned logical block handle
    bool              bulk_owned;       // render frees the owned payload after consume or drop (bulk/text/ui submit helpers set this)
    uint64_t          stream_seq;       // RCMD_STREAM_TRANSFORMS: published ring-slice token
    uint32_t          stream_count;     // RCMD_STREAM_TRANSFORMS: entries in the slice
};

// Enqueue one command. Backpressure retains caller ownership for a safe retry.
[[nodiscard]] RenderResult<> render_submit(
    AnoRenderBridge *bridge, const RenderCommand *cmd);

// Bulk endpoints. Each copies into one render-owned block; caller arrays live until return.
// zero count = ACCEPTED no-op
// INVALID = NULL batch; NULL render_ids with nonzero count; NULL array for a named field; packed size > size_t
[[nodiscard]] RenderResult<> render_submit_bulk_update(
    AnoRenderBridge *bridge, const RenderUpdateBatch *batch);
[[nodiscard]] RenderResult<> render_submit_bulk_destroy(
    AnoRenderBridge *bridge, const uint32_t *render_ids, uint32_t count);

// Streamed-transform lane (ANO_MOTION_STREAMED). Backpressure drops the tick;
// the last published slice repeats. Fill ids/xforms, then commit.
// Single-producer; valid after init.
[[nodiscard]] RenderResult<AnoStreamRegion> render_stream_begin(void);
[[nodiscard]] RenderResult<> render_stream_commit(
    const AnoStreamRegion *region, uint32_t count);

// Runtime lights on a parent renderable (producer light_id; model-space offset). Same backpressure as submit.
// Parent DESTROY detaches implicitly. attach: light_id unmapped; parent CREATE first in ring order.
// update: full params + offset. detach: idempotent.
[[nodiscard]] RenderResult<> render_light_attach(AnoRenderBridge *bridge, uint32_t light_id, uint32_t parent_render_id,
        const RenderLightParams *params, float ox, float oy, float oz);

[[nodiscard]] RenderResult<> render_light_update(AnoRenderBridge *bridge, uint32_t light_id,
        const RenderLightParams *params, float ox, float oy, float oz);

// Partial update: only fields named in `fields` written. Same backpressure contract.
[[nodiscard]] RenderResult<> render_light_update_fields(AnoRenderBridge *bridge, uint32_t light_id,
        const RenderLightParams *params, float ox, float oy, float oz, uint32_t fields);

[[nodiscard]] RenderResult<> render_light_detach(
    AnoRenderBridge *bridge, uint32_t light_id);

// Screen-text blocks. set copies/replaces block text_id (count capped at ANO_RENDER_TEXT_MAX, still ACCEPTED).
// clear idempotent. count 0 set -> clear. INVALID: count > 0 with NULL instances.
// clear: ACCEPTED or BACKPRESSURE only. Retry BACKPRESSURE if one-shot must not miss.
[[nodiscard]] RenderResult<> render_text_set(AnoRenderBridge *bridge, uint32_t text_id,
        const AnoGlyphInstance *instances, uint32_t count);

[[nodiscard]] RenderResult<> render_text_clear(
    AnoRenderBridge *bridge, uint32_t text_id);

// UI blocks (docs/ui/ui-render.md §3.9). set packs/replaces block ui_id; caller arrays live until return.
// clear: idempotent; ACCEPTED or BACKPRESSURE only.
// empty builder -> clear. NULL builder = INVALID.
// INVALID also: per-block caps; glyphCount > 0 with NULL glyphs; bad refs; UI_PATH walk past stream.
[[nodiscard]] RenderResult<> render_ui_set(AnoRenderBridge *bridge, uint32_t ui_id, uint32_t layer,
        const AnoUiBuilder *ui,
        const AnoGlyphInstance *glyphs, uint32_t glyphCount);

[[nodiscard]] RenderResult<> render_ui_clear(
    AnoRenderBridge *bridge, uint32_t ui_id);

// ---------------------------------------------------------------------------
// Back-channel: render -> logic
// ---------------------------------------------------------------------------
// Render owns window/GLFW/camera; logic owns gameplay. Three lanes:
//   - events    : SPSC RenderEvent ring (lossless lifetime facts; INPUT best-effort under flood)
//   - snapshot  : latest-wins RenderSnapshot (view-0 camera/viewport)
//   - viewstate : logic->render AnoViewState (logic owns camera)
// Discrete facts = ring; continuous state = published double buffer.

// Sentinel render_id for "the cursor is over no renderable" in a REVENT_PICK_RESULT.
#define ANO_RENDER_NO_PICK 0xFFFFFFFFu

enum class AnoInputPayloadKind : uint8_t { key, button, cursor, scroll, focus, resize, character };
struct AnoInputContract final { AnoInputPayloadKind payload; };

// Input kinds. GLFW codes forwarded as stable ints. New device = AnoInputKind + union arm.
enum AnoInputKind {
    ANO_INPUT_KEY [[=AnoInputContract{AnoInputPayloadKind::key}]],
    ANO_INPUT_MOUSE_BUTTON [[=AnoInputContract{AnoInputPayloadKind::button}]],
    ANO_INPUT_CURSOR_POS [[=AnoInputContract{AnoInputPayloadKind::cursor}]],
    ANO_INPUT_SCROLL [[=AnoInputContract{AnoInputPayloadKind::scroll}]],
    ANO_INPUT_FOCUS [[=AnoInputContract{AnoInputPayloadKind::focus}]],
    ANO_INPUT_FRAMEBUFFER_RESIZE [[=AnoInputContract{AnoInputPayloadKind::resize}]],
    ANO_INPUT_CHAR [[=AnoInputContract{AnoInputPayloadKind::character}]],
};

struct AnoKeyInputEvent { int32_t key, scancode, action, mods; };
struct AnoButtonInputEvent { int32_t button, action, mods; };
struct AnoCursorInputEvent { float x, y; };
struct AnoScrollInputEvent { float dx, dy; };
struct AnoFocusInputEvent { int32_t focused; };
struct AnoResizeInputEvent { uint32_t width, height; };
struct AnoCharInputEvent { uint32_t codepoint; };
struct AnoInputPayloadFor final { AnoInputKind kind; AnoInputPayloadKind payload; };

union AnoInputPayload {
    AnoKeyInputEvent key [[=AnoInputPayloadFor{ANO_INPUT_KEY, AnoInputPayloadKind::key}]];
    AnoButtonInputEvent button [[=AnoInputPayloadFor{ANO_INPUT_MOUSE_BUTTON, AnoInputPayloadKind::button}]];
    AnoCursorInputEvent cursor [[=AnoInputPayloadFor{ANO_INPUT_CURSOR_POS, AnoInputPayloadKind::cursor}]];
    AnoScrollInputEvent scroll [[=AnoInputPayloadFor{ANO_INPUT_SCROLL, AnoInputPayloadKind::scroll}]];
    AnoFocusInputEvent focus [[=AnoInputPayloadFor{ANO_INPUT_FOCUS, AnoInputPayloadKind::focus}]];
    AnoResizeInputEvent resize [[=AnoInputPayloadFor{ANO_INPUT_FRAMEBUFFER_RESIZE, AnoInputPayloadKind::resize}]];
    AnoCharInputEvent ch [[=AnoInputPayloadFor{ANO_INPUT_CHAR, AnoInputPayloadKind::character}]];
};

// One input sample. Fixed-size POD, sub-tagged on `kind`; rides the events ring inside a RenderEvent.
struct AnoInputEvent {
    uint32_t kind;
    AnoInputPayload u; // key is the largest arm (16 B); cursor is in overlay logical units
};

enum class AnoRenderEventPayloadKind : uint8_t { none, render_id, input, pick_render_id, batch_token };
struct AnoRenderEventContract final { AnoRenderEventPayloadKind payload; };

// Render->logic events. Render master sole producer; logic sole consumer (render_poll_event).
enum RenderEventKind {
    REVENT_SLOT_RETIRED [[=AnoRenderEventContract{AnoRenderEventPayloadKind::render_id}]],
    REVENT_CAPACITY [[=AnoRenderEventContract{AnoRenderEventPayloadKind::none}]],
    REVENT_INPUT [[=AnoRenderEventContract{AnoRenderEventPayloadKind::input}]],
    REVENT_PICK_RESULT [[=AnoRenderEventContract{AnoRenderEventPayloadKind::pick_render_id}]],
    REVENT_BATCH_CONSUMED [[=AnoRenderEventContract{AnoRenderEventPayloadKind::batch_token}]],
};

struct AnoRenderEventPayloadFor final { RenderEventKind kind; AnoRenderEventPayloadKind payload; };

union AnoRenderEventPayload {
    uint32_t render_id [[=AnoRenderEventPayloadFor{REVENT_SLOT_RETIRED, AnoRenderEventPayloadKind::render_id}]];
    AnoInputEvent input [[=AnoRenderEventPayloadFor{REVENT_INPUT, AnoRenderEventPayloadKind::input}]];
    uint32_t pick_render_id [[=AnoRenderEventPayloadFor{REVENT_PICK_RESULT, AnoRenderEventPayloadKind::pick_render_id}]];
    uint64_t batch_token [[=AnoRenderEventPayloadFor{REVENT_BATCH_CONSUMED, AnoRenderEventPayloadKind::batch_token}]];
};

struct RenderEvent {
    RenderEventKind kind;
    AnoRenderEventPayload u;
};

// Latest-wins view-0 camera for picking rays and LOD. Published per recorded frame. View 0 only.
struct RenderSnapshot {
    mat4     viewProj;     // proj * view for view 0 this frame (column-major)
    mat4     invViewProj;  // unproject a cursor texel to a world-space picking ray
    Vector4  frustum[6];   // view-0 frustum planes (same packing as the cull pass)
    uint32_t vpWidth;      // framebuffer extent the matrices were built for
    uint32_t vpHeight;
    // Overlay surface in logical units: uiWidth/uiHeight = framebuffer / uiScale.
    // uiScale = platform content scale. UI layout and cursor events live here.
    float    uiWidth;
    float    uiHeight;
    float    uiScale;
    uint64_t frameId;      // monotonically increasing render frame counter
};

// View-0 camera pose for the renderer. Pose only; renderer owns projection. Latest-wins.
// Until first publish, renderer uses the built-in camera. eye/center/up = lookAt.
struct AnoViewState {
    float    eye[3];
    float    center[3];
    float    up[3];
    float    fovYDeg;    // vertical FOV, degrees
    uint64_t seq;        // producer's monotonic publish counter (diagnostics)
};

// Logic master endpoints. Publish/consume counterparts are private in src/render_bridge/.

// Dequeue next render->logic event. false if none. Drain every tick.
[[nodiscard]] RenderResult<bool> render_poll_event(
    AnoRenderBridge *bridge, RenderEvent *out);

// Copy latest RenderSnapshot into `out`. false if no frame published yet.
[[nodiscard]] RenderResult<bool> render_acquire_snapshot(
    AnoRenderBridge *bridge, RenderSnapshot *out);

// Publish view-0 camera for the next recorded frame. Latest-wins; at most once per logic tick.
// Degenerate pose rejected (previous stands; warn once). Before any accept: built-in camera.
[[nodiscard]] RenderResult<> render_publish_view(
    AnoRenderBridge *bridge, const AnoViewState *view);

// Occlusion model from next recorded frame. Render thread only. L key cycles. Out-of-range ignored.
[[nodiscard]] RenderResult<> render_set_lighting_mode(AnoLightingMode mode);
AnoLightingMode render_get_lighting_mode(void);
const char     *render_lighting_mode_name(AnoLightingMode mode);

// Per-view screen-area cull (projected bounding-sphere radius, px). Below threshold: no draw.
// 0 disables; negative clamps to 0; bad view ignored. Next recorded frame; render thread.
[[nodiscard]] RenderResult<> render_set_view_cull_threshold(
    uint32_t view, float pixels);
[[nodiscard]] RenderResult<float> render_get_view_cull_threshold(uint32_t view);

// Per-view LOD threshold (projected bounding-sphere radius, px). Halving size drops one LOD level.
// 0 = always finest; negative clamps to 0. Inert without LOD chains. Next frame; render thread.
[[nodiscard]] RenderResult<> render_set_view_lod_threshold(
    uint32_t view, float pixels);
[[nodiscard]] RenderResult<float> render_get_view_lod_threshold(uint32_t view);

// Global LOD-level bias added to auto-selected level (clamped per mesh). Next frame; render thread.
void    render_set_lod_bias(int32_t bias);
int32_t render_get_lod_bias(void);

// Shadow caster LOD bias (global; no per-caster screen metric). Clamped [0, max LOD]. Next frame; render thread.
void    render_set_shadow_lod_bias(int32_t bias);
int32_t render_get_shadow_lod_bias(void);

// Per-view GPU Hi-Z occlusion cull (previous-frame depth; ~1 frame latency). Off by default.
// Next frame; render thread. Bad view ignored.
[[nodiscard]] RenderResult<> render_set_view_hiz_enable(
    uint32_t view, bool enable);
[[nodiscard]] RenderResult<bool> render_get_view_hiz_enable(uint32_t view);


} // namespace ano
