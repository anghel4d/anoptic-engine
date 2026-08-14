#ifndef ANOPTICENGINE_COOKER_INTERNAL_H
#define ANOPTICENGINE_COOKER_INTERNAL_H

#include <anoptic_resources_cook.h>

using namespace ano;

#include <anoptic_memory_typed.h>

#include "parallel.h"

struct AnoResourceCookCheckpoint final {
    uint64_t itemCount;
    uint64_t volumeCount;
    AnoAssetId nextDerivedAsset;
};

struct AnoResourcePackItem final {
    AnoAssetId asset;
    AnoResourceSourceId source;
    AnoResourceTypeId type;
    uint64_t producer;
    AnoContentId inputIdentity;
    AnoResourceCommitGroupId commitGroup;
    AnoResourceBytes artifact;
    AnoResourceSchema schema;
    AnoContentId content;
    const AnoResourceDependency *dependencies;
    uint64_t dependencyCount;
    ano::MemoryVolume *volume;
    ano::MemoryReservation reservation;
};

struct AnoResourceRevisionStorage final {
    AnoResourceSourceId source;
    ano::MemoryVolume *volume;
    ano::MemoryReservation artifact;
};

struct ano::AnoCookedRevision final {
    size_t references;
    AnoResourceManifestEntry *entries;
    AnoResourceRevisionStorage *storage;
    uint64_t itemCount;
    AnoResourceDependency *dependencies;
    uint64_t dependencyCount;
    ano::MemoryVolume **volumes;
    uint64_t volumeCount;
    bool validated;
};

namespace ano::resource_detail {

inline int compare_dependency(const void *left, const void *right)
{
    const auto& lhs = *static_cast<const AnoResourceDependency *>(left);
    const auto& rhs = *static_cast<const AnoResourceDependency *>(right);
    if (lhs.asset.value != rhs.asset.value)
        return lhs.asset.value < rhs.asset.value ? -1 : 1;
    if (lhs.type.value != rhs.type.value)
        return lhs.type.value < rhs.type.value ? -1 : 1;
    return 0;
}

template<class Item>
AnoResourceError validate_dependency_graph(
    const Item *items, uint64_t itemCount,
    const AnoResourceDependency *dependencies, uint64_t dependencyCount)
{
    if ((items == nullptr && itemCount != 0)
        || (dependencies == nullptr && dependencyCount != 0)
        || itemCount > SIZE_MAX)
        return ANO_RESOURCE_BAD_MANIFEST;
    uint64_t dependencyCursor = 0;
    for (uint64_t i = 0; i < itemCount; ++i) {
        const Item& item = items[i];
        if (item.asset.value != i + 1 || item.type.value == 0
            || item.producer == 0 || item.commitGroup.value == 0
            || item.dependencyFirst != dependencyCursor
            || dependencyCursor > dependencyCount
            || item.dependencyCount > dependencyCount - dependencyCursor)
            return ANO_RESOURCE_BAD_MANIFEST;
        for (uint64_t d = 0; d < item.dependencyCount; ++d) {
            const AnoResourceDependency& dependency =
                dependencies[item.dependencyFirst + d];
            if (dependency.asset.value == 0
                || dependency.asset.value > itemCount
                || items[dependency.asset.value - 1].type.value
                    != dependency.type.value
                || (d != 0 && compare_dependency(
                        dependencies + item.dependencyFirst + d - 1,
                        &dependency) >= 0))
                return ANO_RESOURCE_BAD_MANIFEST;
        }
        if (!ano::checked_add(
                dependencyCursor, item.dependencyCount, &dependencyCursor))
            return ANO_RESOURCE_BAD_MANIFEST;
    }
    if (dependencyCursor != dependencyCount)
        return ANO_RESOURCE_BAD_MANIFEST;

    uint64_t stackCount = 0;
    if (!ano::checked_multiply(itemCount, UINT64_C(2), &stackCount)
        || stackCount > SIZE_MAX / sizeof(uint64_t))
        return ANO_RESOURCE_OVERFLOW;
    uint8_t *state = itemCount == 0 ? nullptr
        : mi_calloc_tp(uint8_t, static_cast<size_t>(itemCount));
    uint64_t *stack = stackCount == 0 ? nullptr
        : mi_mallocn_tp(uint64_t, static_cast<size_t>(stackCount));
    if (itemCount != 0 && (state == nullptr || stack == nullptr)) {
        mi_free(stack);
        mi_free(state);
        return ANO_RESOURCE_OUT_OF_MEMORY;
    }
    AnoResourceError result = ANO_RESOURCE_OK;
    for (uint64_t root = 0; root < itemCount && result == ANO_RESOURCE_OK;
         ++root) {
        if (state[root] != 0)
            continue;
        uint64_t depth = 0;
        stack[depth++] = root;
        stack[depth++] = 0;
        state[root] = 1;
        while (depth != 0 && result == ANO_RESOURCE_OK) {
            const uint64_t node = stack[depth - 2];
            uint64_t& next = stack[depth - 1];
            const Item& item = items[node];
            if (next == item.dependencyCount) {
                state[node] = 2;
                depth -= 2;
                continue;
            }
            const uint64_t target = dependencies[
                item.dependencyFirst + next++].asset.value - 1;
            if (state[target] == 1) {
                result = ANO_RESOURCE_BAD_MANIFEST;
            } else if (state[target] == 0) {
                state[target] = 1;
                stack[depth++] = target;
                stack[depth++] = 0;
            }
        }
    }
    mi_free(stack);
    mi_free(state);
    return result;
}

} // namespace ano::resource_detail

AnoResourceCookCheckpoint ano_resource_cooker_checkpoint(
    const AnoResourceCooker *cooker);
void ano_resource_cooker_rollback(AnoResourceCooker *cooker,
                                  AnoResourceCookCheckpoint checkpoint);
bool ano_resource_cooker_root_valid(const AnoResourceCooker *cooker,
                                    AnoAssetId root);
const char *ano_resource_cooker_source_path(
    const AnoResourceCooker *cooker, AnoResourceSourceId source);
AnoResourceError ano_resource_cooker_allocate_derived(
    AnoResourceCooker *cooker, AnoAssetId *asset);
bool ano_resource_cooker_cancelled(const AnoResourceCooker *cooker);
AnoResourceError ano_resource_cooker_parallel(
    AnoResourceCooker *cooker, uint64_t count, void *context,
    ano::resource_detail::ParallelFunction function);
AnoResourceError ano_resource_cooker_import_begin(
    AnoResourceCooker *cooker, AnoResourceSourceId source, bool *execute);
AnoResourceError ano_resource_cooker_import_end(
    AnoResourceCooker *cooker, AnoResourceSourceId source, bool success);
AnoResourceError ano_resource_cooker_source_dependencies(
    AnoResourceCooker *cooker, const char *const *paths, uint64_t count);
AnoResourceError ano_resource_cooker_source_bytes(
    AnoResourceCooker *cooker, const char *path, AnoResourceBytes *bytes);
AnoResourceError ano_resource_cooker_source_content(
    AnoResourceCooker *cooker, const char *path, AnoContentId *content);
bool ano_resource_cooker_source_changed(
    const AnoResourceCooker *cooker, const char *path);
bool ano_resource_cooker_import_all(const AnoResourceCooker *cooker);
void ano_resource_cooker_select_producer(
    AnoResourceCooker *cooker, uint64_t producer);
AnoResourceError ano_resource_cooker_action_required(
    AnoResourceCooker *cooker, AnoAssetId asset, AnoResourceTypeId type,
    AnoResourceCommitGroupId commitGroup, AnoContentId inputIdentity,
    bool *required);
AnoResourceError ano_resource_revision_validate(const AnoCookedRevision *revision);
const AnoResourceManifestEntry *ano_resource_revision_entry(
    const AnoCookedRevision *revision, AnoAssetId asset);
AnoResourceError ano_resource_revision_dependencies(
    const AnoCookedRevision *revision, AnoAssetId asset,
    const AnoResourceDependency **dependencies, uint64_t *count);
AnoResourceError ano_resource_pack_build(
    const AnoCookedRevision *revision,
    AnoResourceMutableBytes *pack);

#endif // ANOPTICENGINE_COOKER_INTERNAL_H
