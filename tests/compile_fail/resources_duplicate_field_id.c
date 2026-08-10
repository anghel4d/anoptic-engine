#include <anoptic_resources_typed.h>

namespace invalid_schema {

struct [[=ano::Artifact{ano::wire_id("duplicate-fields"),
                        ano::Storage::portable, 1}]] Record final {
    [[=ano::Field{1, ano::FieldPolicy::required}]] uint32_t first;
    [[=ano::Field{1, ano::FieldPolicy::required}]] uint32_t second;
};

} // namespace invalid_schema

inline constexpr auto invalidLanguage = ano::compile_resource_language(
    ^^invalid_schema, ano::current_resource_profile());
