/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0
 * Anoptic targets ISO C++26. */

#include <cstddef>
#include <cstdint>
#include <meta>

namespace ano::proof {

/* Templates index semantic types. They do not perform structural computation. */
template<class Semantic>
struct AssetRef final {
    std::uint64_t identity;
};

template<class Value, class Error>
struct Result final {
    bool succeeded;
    union {
        Value value;
        Error error;
    };

    static constexpr Result ok(Value value) noexcept
    {
        return {.succeeded = true, .value = value};
    }

    static constexpr Result fail(Error error) noexcept
    {
        return {.succeeded = false, .error = error};
    }
};

struct Source final {
    std::uint32_t bytes;
};

struct Texture final {
    std::uint32_t width;
    std::uint32_t height;
    AssetRef<Source> provenance;
};

struct InvalidEmpty final {};

consteval bool is_nonempty_record(std::meta::info type)
{
    return std::meta::is_class_type(type)
        && !std::meta::nonstatic_data_members_of(
                type, std::meta::access_context::current()).empty();
}

consteval bool has_only_type_template_arguments(
    std::meta::info type, std::size_t expectedCount)
{
    if (!std::meta::has_template_arguments(type))
        return false;
    const auto arguments = std::meta::template_arguments_of(type);
    if (arguments.size() != expectedCount)
        return false;
    for (const std::meta::info argument : arguments)
        if (!std::meta::is_type(argument))
            return false;
    return true;
}

consteval std::size_t reflected_member_count(std::meta::info type)
{
    return std::meta::nonstatic_data_members_of(
        type, std::meta::access_context::current()).size();
}

/* Expansion and type splicing derive a direct operation from one declaration. */
consteval std::size_t reflected_texture_field_bytes()
{
    static constexpr auto fields = std::define_static_array(
        std::meta::nonstatic_data_members_of(
            ^^Texture, std::meta::access_context::current()));
    std::size_t result = 0;
    template for (constexpr std::meta::info field : fields) {
        using Field = [:std::meta::type_of(field):];
        result += sizeof(Field);
    }
    return result;
}

static_assert(is_nonempty_record(^^Texture));
static_assert(!is_nonempty_record(^^InvalidEmpty));
static_assert(reflected_member_count(^^Texture) == 3);
static_assert(has_only_type_template_arguments(^^AssetRef<Texture>, 1));
static_assert(has_only_type_template_arguments(^^Result<Texture, std::uint32_t>, 2));
static_assert(reflected_texture_field_bytes()
              == sizeof(std::uint32_t) * 2 + sizeof(AssetRef<Source>));

/* One constexpr meaning is valid during translation and execution. */
constexpr std::uint32_t checked_increment(std::uint32_t value) noexcept
{
    return value + 1;
}

constexpr std::uint32_t checked_double(std::uint32_t value) noexcept
{
    return value * 2;
}

constexpr std::uint32_t pure_composition(std::uint32_t value) noexcept
{
    return checked_double(checked_increment(value));
}

enum class Error : std::uint8_t {
    rejected,
};

constexpr Result<std::uint32_t, Error> parse(std::uint32_t value) noexcept
{
    if (value == 0)
        return Result<std::uint32_t, Error>::fail(Error::rejected);
    return Result<std::uint32_t, Error>::ok(value + 1);
}

constexpr Result<std::uint32_t, Error> cook(std::uint32_t value) noexcept
{
    return Result<std::uint32_t, Error>::ok(value * 2);
}

constexpr Result<std::uint32_t, Error> fallible_composition(
    std::uint32_t value) noexcept
{
    const Result<std::uint32_t, Error> parsed = parse(value);
    if (!parsed.succeeded)
        return Result<std::uint32_t, Error>::fail(parsed.error);
    return cook(parsed.value);
}

struct Counter final {
    std::uint32_t value;
};

struct StatefulValue final {
    std::uint32_t value;
    Counter state;
};

constexpr StatefulValue stateful_first(
    std::uint32_t input, Counter state) noexcept
{
    ++state.value;
    return {input + state.value, state};
}

constexpr StatefulValue stateful_second(
    std::uint32_t input, Counter state) noexcept
{
    ++state.value;
    return {input * state.value, state};
}

constexpr StatefulValue stateful_composition(
    std::uint32_t input, Counter state) noexcept
{
    const StatefulValue middle = stateful_first(input, state);
    return stateful_second(middle.value, middle.state);
}

struct Publication final {
    std::uint32_t generation;
    std::uint32_t value;
};

constexpr Publication transactional_replace(
    Publication before, Result<std::uint32_t, Error> candidate) noexcept
{
    if (!candidate.succeeded)
        return before;
    return {before.generation + 1, candidate.value};
}

struct RouteInputs final {
    AssetRef<Source> source;
    std::uint32_t settings;
};

struct RouteOutputs final {
    AssetRef<Texture> texture;
};

constexpr RouteOutputs resource_route(RouteInputs inputs) noexcept
{
    return {{inputs.source.identity ^ inputs.settings}};
}

static_assert(pure_composition(4) == 10);
static_assert(fallible_composition(4).succeeded
              && fallible_composition(4).value == 10);
static_assert(!fallible_composition(0).succeeded);
static_assert(stateful_composition(2, {0}).value == 6);
static_assert(stateful_composition(2, {0}).state.value == 2);
static_assert(transactional_replace({7, 11}, parse(0)).generation == 7);
static_assert(transactional_replace({7, 11}, parse(4)).generation == 8);
static_assert(resource_route({{41}, 3}).texture.identity == 42);

} // namespace ano::proof

int main()
{
    return ano::proof::pure_composition(4) == 10 ? 0 : 1;
}
