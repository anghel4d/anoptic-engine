#include <anoptic_resources_typed.h>

namespace invalid_schema {

struct [[=ano::Artifact{}]] Source final {
    uint32_t value;
};

struct [[=ano::Artifact{}]] Output final {
    uint32_t value;
};

[[=ano::Transform{ano::Executor::worker, ano::Streaming::whole, true}]]
bool transform(Source source, Output output) noexcept;

} // namespace invalid_schema

static_assert(ano::compile_resource_language(^^invalid_schema));
