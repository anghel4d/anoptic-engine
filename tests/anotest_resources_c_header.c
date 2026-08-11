#include <anoptic_resources_runtime.h>

int main(void)
{
    AnoAssetId asset = {1};
    AnoResourceBytes bytes = {0};
    AnoResourceManifestEntry entry = {0};
    AnoResourceGoal goal = {0};
    return asset.value == 1 && bytes.size == 0 && entry.asset.value == 0
            && goal.goal.value == 0
        ? 0
        : 1;
}
