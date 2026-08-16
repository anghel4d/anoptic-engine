/* SPDX-FileCopyrightText: 2026 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0
 * Anoptic targets ISO C++26. */

#include <meta>

#ifndef __cpp_impl_reflection
#error "C++26 reflection is required"
#endif

static_assert(__cpp_impl_reflection >= 202506L);
static_assert(__cpp_expansion_statements >= 202506L);

namespace {

struct Marker final {
    int value;
};

struct [[=Marker{7}]] Record final {
    int first;
    int second;
};

struct Base {
    int value;
};

struct Derived final : Base {
    int extra;
};

int operation(const Record&) noexcept;

struct Callable final {
    constexpr int operator()(int value) const noexcept { return value + 1; }
};

template<class>
struct Carrier;

using RecordAlias = Record;

consteval bool supports_anoptic_cxx26()
{
    static_assert(std::meta::is_type(^^Record));
    static_assert(std::meta::dealias(^^RecordAlias) == ^^Record);
    static_assert(std::meta::template_of(^^Carrier<int>) == ^^Carrier);

    constexpr auto annotations = std::define_static_array(
        std::meta::annotations_of_with_type(^^Record, ^^Marker));
    static_assert(annotations.size() == 1);
    static_assert(std::meta::extract<Marker>(annotations[0]).value == 7);

    static constexpr auto members = std::define_static_array(
        std::meta::nonstatic_data_members_of(
            ^^Record, std::meta::access_context::unchecked()));
    Record record{2, 3};
    int sum = 0;
    template for (constexpr std::meta::info member : members)
        sum += record.[:member:];

    constexpr auto parameters = std::define_static_array(
        std::meta::parameters_of(^^operation));
    static_assert(parameters.size() == 1);
    static_assert(std::meta::remove_cvref(std::meta::type_of(parameters[0]))
                  == ^^Record);
    static_assert(std::meta::return_type_of(^^operation) == ^^int);
    static_assert(std::meta::is_noexcept(^^operation));

    bool foundCall = false;
    static constexpr auto declarations = std::define_static_array(
        std::meta::members_of(
            ^^Callable, std::meta::access_context::unchecked()));
    template for (constexpr std::meta::info declaration : declarations) {
        if constexpr (std::meta::is_operator_function(declaration)
                      && std::meta::operator_of(declaration)
                          == std::meta::op_parentheses)
            foundCall = true;
    }

    constexpr auto bases = std::define_static_array(
        std::meta::bases_of(
            ^^Derived, std::meta::access_context::unchecked()));
    Derived derived{{11}, 5};
    Base& base = derived.[:bases[0]:];

    constexpr const char* name = std::define_static_string("anoptic");
    constexpr const Marker* marker = std::define_static_object(Marker{13});

    return sum == 5 && foundCall && base.value == 11
        && name[0] == 'a' && name[6] == 'c' && marker->value == 13;
}

static_assert(supports_anoptic_cxx26());

} // namespace

int main() {}
