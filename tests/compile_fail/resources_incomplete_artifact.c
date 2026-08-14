#include <anoptic_resources_typed.h>

using namespace ano;

namespace invalid_schema {

struct [[=ano::Artifact{}]] Record;

} // namespace invalid_schema

static_assert(ano::compile_resource_language(^^invalid_schema));
