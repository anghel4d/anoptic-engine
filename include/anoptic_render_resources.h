/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Portable render artifacts consumed by renderer-owned realization transforms.

#ifndef ANOPTICENGINE_ANOPTIC_RENDER_RESOURCES_H
#define ANOPTICENGINE_ANOPTIC_RENDER_RESOURCES_H

#include "anoptic_resources_typed.h"

namespace ano::asset_schema {

struct [[=Artifact{wire_id("ano.render.texture"), Storage::portable, 1}]]
Texture final {
    [[=Field{1, FieldPolicy::required}]] uint32_t width;
    [[=Field{2, FieldPolicy::required}]] uint32_t height;
    [[=Field{3, FieldPolicy::required}]] uint32_t mipCount;
    [[=Field{4, FieldPolicy::required}]] uint32_t format;
    [[=Field{5, FieldPolicy::required}]] RelativeSpan<uint8_t> bytes;
};

struct [[=Artifact{wire_id("ano.render.material"), Storage::portable, 1}]]
Material final {
    [[=Field{1, FieldPolicy::required}]] float baseColorFactor[4];
    [[=Field{2, FieldPolicy::required}]] float emissiveFactor[3];
    [[=Field{3, FieldPolicy::required}]] float metallicFactor;
    [[=Field{4, FieldPolicy::required}]] float roughnessFactor;
    [[=Field{5, FieldPolicy::required}]] float normalScale;
    [[=Field{6, FieldPolicy::required}]] float occlusionStrength;
    [[=Field{7, FieldPolicy::required}]] float alphaCutoff;
    [[=Field{8, FieldPolicy::required}]] uint32_t flags;
    [[=Field{9, FieldPolicy::dependency}]] AssetRef<Texture> baseColorTexture;
    [[=Field{10, FieldPolicy::dependency}]] AssetRef<Texture> metallicRoughnessTexture;
    [[=Field{11, FieldPolicy::dependency}]] AssetRef<Texture> normalTexture;
    [[=Field{12, FieldPolicy::dependency}]] AssetRef<Texture> occlusionTexture;
    [[=Field{13, FieldPolicy::dependency}]] AssetRef<Texture> emissiveTexture;
};

struct Vertex final {
    [[=Field{1, FieldPolicy::required}]] float position[3];
    [[=Field{2, FieldPolicy::required}]] float normal[3];
    [[=Field{3, FieldPolicy::required}]] float texCoord[2];
};

struct [[=Artifact{wire_id("ano.render.mesh"), Storage::portable, 1}]]
Mesh final {
    [[=Field{1, FieldPolicy::required}]] RelativeSpan<Vertex> vertices;
    [[=Field{2, FieldPolicy::required}]] RelativeSpan<uint32_t> indices;
    [[=Field{3, FieldPolicy::dependency}]] AssetRef<Material> material;
    [[=Field{4, FieldPolicy::required}]] float boundsMinimum[3];
    [[=Field{5, FieldPolicy::required}]] float boundsMaximum[3];
};

} // namespace ano::asset_schema

#endif // ANOPTICENGINE_ANOPTIC_RENDER_RESOURCES_H
