#include <anoptic_resources_typed.h>

namespace invalid_schema {

struct [[=ano::Artifact{}]] Record final {
    const uint8_t *bytes;
};

} // namespace invalid_schema

static_assert(ano::compile_resource_language(^^invalid_schema));
