#include <anoptic_resources_typed.h>

namespace invalid_schema {

struct [[=ano::Artifact{ano::wire_id("pointer-field"),
                        ano::Storage::portable, 1}]] Record final {
    [[=ano::Field{1, ano::FieldPolicy::required}]] const uint8_t *bytes;
};

} // namespace invalid_schema

inline constexpr auto invalidLanguage = ano::compile_resource_language(
    ^^invalid_schema, ano::current_resource_profile());
