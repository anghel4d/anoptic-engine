#ifndef ANO_MESHOPTIMIZER_H
#define ANO_MESHOPTIMIZER_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
// No foreign consumer is currently identified; retain this C linkage pending removal.
extern "C" {
#endif


/* Types */

typedef struct {
    uint32_t vertex_offset;
    uint32_t triangle_offset;
    uint32_t vertex_count;
    uint32_t triangle_count;
} ano_meshlet_t;

/* Vulkan std430 / scalar buffer layout: 16-byte rows. */
typedef struct {
    float center[3];     float radius;
    float cone_apex[3];  float cone_cutoff;
    float cone_axis[3];  float padding;
} ano_meshlet_bounds_gpu_t;


/* Meshlets */

// Worst-case meshlet count (meshlets[] capacity).
size_t ano_build_meshlets_bound(size_t index_count, size_t max_vertices, size_t max_triangles);

// destination: index_count elements; may alias indices.
void ano_optimize_vertex_cache(uint32_t* destination, const uint32_t* indices, size_t index_count, size_t vertex_count);

// Linear packing. Prefer cache-optimized indices.
// meshlets: ano_build_meshlets_bound capacity.
// meshlet_vertices: max_meshlets * max_vertices.
// meshlet_triangles: max_meshlets * max_triangles * 3 local indices.
size_t ano_build_meshlets(ano_meshlet_t* meshlets, uint32_t* meshlet_vertices, uint8_t* meshlet_triangles, 
                          const uint32_t* indices, size_t index_count, 
                          size_t max_vertices, size_t max_triangles);

// Ritter sphere + meshopt normal cone (axis = mean unit normal,
// cutoff = sin(max deviation), 1 past hemisphere). flat.task cull:
// dot(center-eye, axis) >= cutoff*|center-eye| + radius.
ano_meshlet_bounds_gpu_t ano_compute_meshlet_bounds(const uint32_t* meshlet_vertices, const uint8_t* meshlet_triangles,
                                                    size_t triangle_count, const float* vertex_positions,
                                                    size_t vertex_count, size_t vertex_positions_stride);


/* Simplify */

// Largest bbox axis. target_error is relative to this; out_result_error is object units.
float ano_simplify_scale(const float* vertex_positions, size_t vertex_count, size_t vertex_positions_stride);

// QEM collapse. Same VBO, shorter IBO. Geometry-only: welds coincident positions; borders slide along border only.
// destination: index_count slots; may alias indices.
// target_index_count: desired count (may stop early on locked topology).
// target_error: max relative error vs ano_simplify_scale.
// out_result_error: achieved error in object units (nullable).
// Returns index count (multiple of 3). Drops coincident-position degenerates.
size_t ano_simplify(uint32_t* destination, const uint32_t* indices, size_t index_count,
                    const float* vertex_positions, size_t vertex_count, size_t vertex_positions_stride,
                    size_t target_index_count, float target_error, float* out_result_error);

// Default edge_len_factor: max edge = this * mean source edge.
#define ANO_SIMPLIFY_EDGE_FACTOR_DEFAULT 8.0f

// ano_simplify plus growth guards. QEM is ~0 on coplanar flats; cap resulting
// edges at edge_len_factor * mean source edge (unit-extent) so collapses cannot
// bridge a floor/wall. <=0 disables guards (byte-identical to ano_simplify).
// Guards: abs bbox backstop, ~75deg fold, collinear drop, link/tetra, feature-slide
// (dihedral>45), max 60deg cumulative normal drift. Other params match ano_simplify.
size_t ano_simplify_ex(uint32_t* destination, const uint32_t* indices, size_t index_count,
                       const float* vertex_positions, size_t vertex_count, size_t vertex_positions_stride,
                       size_t target_index_count, float target_error, float edge_len_factor,
                       float* out_result_error);

#ifdef __cplusplus
}
#endif

#endif // ANO_MESHOPTIMIZER_H
