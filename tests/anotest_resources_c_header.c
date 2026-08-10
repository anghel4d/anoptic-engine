#include <anoptic_resources.h>

int main(void)
{
    AnoAssetId asset = {1};
    AnoResourceBytes bytes = {0};
    return asset.value == 1 && bytes.size == 0 ? 0 : 1;
}
