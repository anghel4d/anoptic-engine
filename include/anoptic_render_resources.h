/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Portable render artifacts consumed by renderer-owned realization transforms.

#ifndef ANOPTICENGINE_ANOPTIC_RENDER_RESOURCES_H
#define ANOPTICENGINE_ANOPTIC_RENDER_RESOURCES_H

#include "anoptic_resources_typed.h"

namespace ano::asset_schema {

struct [[=Artifact{}]] Texture final {
    uint32_t width;
    uint32_t height;
    uint32_t mipCount;
    uint32_t format;
    RelativeSpan<uint8_t> bytes;
};

struct [[=Artifact{}]] Material final {
    float baseColorFactor[4];
    float emissiveFactor[3];
    float metallicFactor;
    float roughnessFactor;
    float normalScale;
    float occlusionStrength;
    float alphaCutoff;
    uint32_t flags;
    AssetRef<Texture> baseColorTexture;
    AssetRef<Texture> metallicRoughnessTexture;
    AssetRef<Texture> normalTexture;
    AssetRef<Texture> occlusionTexture;
    AssetRef<Texture> emissiveTexture;
};

struct Vertex final {
    float position[3];
    float normal[3];
    float texCoord[2];
};

struct [[=Artifact{}]] Mesh final {
    RelativeSpan<Vertex> vertices;
    RelativeSpan<uint32_t> indices;
    AssetRef<Material> material;
    float boundsMinimum[3];
    float boundsMaximum[3];
};

} // namespace ano::asset_schema

#endif // ANOPTICENGINE_ANOPTIC_RENDER_RESOURCES_H
