#include <anoptic_resources_typed.h>

namespace invalid_schema {

struct [[=ano::Artifact{ano::wire_id("dependency-target"),
                        ano::Storage::portable, 1}]] Target final {
    [[=ano::Field{1, ano::FieldPolicy::required}]] uint32_t value;
};

struct [[=ano::Artifact{ano::wire_id("dependency-owner"),
                        ano::Storage::portable, 1}]] Owner final {
    [[=ano::Field{1, ano::FieldPolicy::required}]] ano::AssetRef<Target> target;
};

} // namespace invalid_schema

inline constexpr auto invalidLanguage = ano::compile_resource_language(
    ^^invalid_schema, ano::current_resource_profile());
