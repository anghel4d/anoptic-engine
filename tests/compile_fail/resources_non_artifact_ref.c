#include <anoptic_resources_typed.h>

namespace invalid_schema {

struct [[=ano::Artifact{}]] Record final {
    ano::AssetRef<uint32_t> target;
};

} // namespace invalid_schema

static_assert(ano::compile_resource_language(^^invalid_schema));
