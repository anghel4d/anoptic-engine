#ifndef ANOPTICENGINE_COOKER_INTERNAL_H
#define ANOPTICENGINE_COOKER_INTERNAL_H

#include <anoptic_resources_cook.h>

struct AnoResourceCookCheckpoint final {
    uint64_t itemCount;
    AnoAssetId nextDerivedAsset;
};

struct AnoResourcePackItem final {
    AnoAssetId asset;
    AnoResourceTypeId type;
    AnoResourceCommitGroupId commitGroup;
    AnoResourceBytes artifact;
};

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
AnoResourceError ano_resource_cooker_adopt(
    AnoResourceCooker *cooker, AnoAssetId asset, AnoResourceTypeId type,
    AnoResourceCommitGroupId commitGroup, uint8_t *artifact,
    uint64_t artifactSize);
bool ano_resource_cooker_cancelled(const AnoResourceCooker *cooker);
AnoResourceError ano_resource_pack_build(
    const AnoResourcePackItem *items, uint64_t itemCount,
    AnoResourceMutableBytes *pack);

#endif // ANOPTICENGINE_COOKER_INTERNAL_H
