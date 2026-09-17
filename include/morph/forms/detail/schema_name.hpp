// SPDX-License-Identifier: Apache-2.0

#pragma once

/// @file forms/detail/schema_name.hpp
/// @brief Compile-time `$defs` keys for the forms layer's field wrappers.
///
/// glaze keys a schema's `$defs` block by `glz::name_v<T>` and populates each
/// entry **once**: `auto& def = defs[name_v<val_t>]; if (!def.type) { … }`. A
/// `glz::meta::name` that is the same string for two instantiations therefore
/// makes the second one silently `$ref` the first one's definition, and a
/// renderer that resolves the `$ref` reads the wrong payload type (morph#543).
///
/// This header is the single place the forms layer composes those keys, so the
/// rule that keeps them apart is stated once rather than once per wrapper.
///
/// Two rules shape what goes into a key:
///
/// - **Nothing compiler-derived.** `glz::name_v` falls back to a
///   `__PRETTY_FUNCTION__`/`__FUNCSIG__`-derived spelling that differs between
///   compilers, and a `$defs` key that changed with the compiler would make a
///   schema unreadable by a peer build — the same reason
///   `core/payload_schema.hpp` refuses it for the payload fingerprint. Keys
///   here are built only from template arguments spelled in these sources.
/// - **Discriminate exactly what the definition's *content* depends on.** Two
///   instantiations whose `$defs` entries would be byte-identical *should*
///   share one entry; that is what `$defs` is for. Splitting them would only
///   bloat the document. So a key carries the parameters the entry's shape
///   varies with, and nothing else.

#include <array>
#include <concepts>
#include <cstddef>
#include <string_view>
#include <type_traits>

#include "../../detail/fixed_string.hpp"

namespace morph::forms::detail {

/// @brief Length of a `FixedString`'s text, excluding its terminating null.
/// @tparam N Storage size including the terminating null.
/// @param text The compile-time string to measure.
/// @return The number of characters in @p text.
template <std::size_t N>
[[nodiscard]] consteval std::size_t fixedStringLength(const ::morph::detail::FixedString<N>& text) noexcept {
    static_cast<void>(text);
    return N - 1;
}

/// @brief Storage for a `Choice`'s `$defs` key.
///
/// `inline constexpr` gives it static storage duration, which is what lets a
/// `std::string_view` into it serve as a `glz::meta::name`.
///
/// The key is composed from every `FixedString` template argument, separated
/// by `_` so that two different splits of the same characters
/// (`DependsOn = {"ab", "c"}` versus `{"a", "bc"}`) cannot alias.
///
/// @tparam OptionsAction Type id of the action serving the options.
/// @tparam ValueField    Result-row field submitted as the value.
/// @tparam LabelField    Result-row field shown to the user.
/// @tparam DependsOn     Wire names of the sibling fields parameterising the
///                       options action.
template <::morph::detail::FixedString OptionsAction, ::morph::detail::FixedString ValueField,
          ::morph::detail::FixedString LabelField, ::morph::detail::FixedString... DependsOn>
inline constexpr auto choiceSchemaNameStorage = [] {
    constexpr std::string_view prefix{"Choice_"};
    constexpr std::size_t total = prefix.size() + fixedStringLength(OptionsAction) + 1U +
                                  fixedStringLength(ValueField) + 1U + fixedStringLength(LabelField) +
                                  ((1U + fixedStringLength(DependsOn)) + ... + std::size_t{0});

    std::array<char, total> out{};
    std::size_t at = 0;
    auto append = [&out, &at](std::string_view part) {
        for (char const character : part) {
            out[at] = character;
            ++at;
        }
    };

    append(prefix);
    append(OptionsAction.view());
    append("_");
    append(ValueField.view());
    append("_");
    append(LabelField.view());
    ((append("_"), append(DependsOn.view())), ...);
    return out;
}();

/// @brief The `$defs` key for one `Choice` instantiation.
///
/// Distinct for every distinct set of `FixedString` arguments. Two `Choice`
/// fields naming the *same* options action **and** the same value field still
/// share a key: they read one column of one result set, which has one type, so
/// a differing `T` between them is an author error rather than a shape this
/// key has to keep apart.
///
/// @tparam OptionsAction Type id of the action serving the options.
/// @tparam ValueField    Result-row field submitted as the value.
/// @tparam LabelField    Result-row field shown to the user.
/// @tparam DependsOn     Wire names of the sibling fields parameterising the
///                       options action.
template <::morph::detail::FixedString OptionsAction, ::morph::detail::FixedString ValueField,
          ::morph::detail::FixedString LabelField, ::morph::detail::FixedString... DependsOn>
inline constexpr std::string_view choiceSchemaName{
    choiceSchemaNameStorage<OptionsAction, ValueField, LabelField, DependsOn...>.data(),
    choiceSchemaNameStorage<OptionsAction, ValueField, LabelField, DependsOn...>.size()};

/// @brief A short, fixed-capacity compile-time text buffer.
///
/// A literal type with public members, so it can be returned from a `consteval`
/// function and read in a constant expression. Sized for the longest tag
/// `numericShapeTag` produces (`"bool"`), with room to spare.
struct ShapeTag {
    /// @brief Character storage; only the first `size` entries are meaningful.
    std::array<char, 8> characters{};
    /// @brief Number of meaningful characters in `characters`.
    std::size_t size = 0;

    /// @brief Appends @p text.
    /// @param text The characters to append.
    constexpr void append(std::string_view text) noexcept {
        for (char const character : text) {
            characters[size] = character;
            ++size;
        }
    }

    /// @brief Appends @p value's decimal digits.
    /// @param value The number to render.
    constexpr void appendDecimal(std::size_t value) noexcept {
        std::array<char, 20> digits{};
        std::size_t count = 0;
        do {
            digits[count] = static_cast<char>('0' + static_cast<char>(value % 10U));
            ++count;
            value /= 10U;
        } while (value != 0);
        while (count > 0) {
            --count;
            characters[size] = digits[count];
            ++size;
        }
    }

    /// @brief A view of the meaningful characters.
    /// @return The rendered tag.
    [[nodiscard]] constexpr std::string_view view() const noexcept { return {characters.data(), size}; }
};

/// @brief A deterministic tag for the JSON-schema shape of an arithmetic type.
///
/// The shape a nullable arithmetic type generates is fixed by three things and
/// no others: whether it is `bool`, whether it is floating-point, whether it is
/// signed, and how wide it is. So the tag carries exactly those — `"bool"`,
/// or `f`/`i`/`u` followed by the width in bits. Two types that share a tag
/// generate byte-identical definitions, which is precisely when sharing one
/// `$defs` entry is correct rather than a collision.
///
/// Derived from `sizeof` and the standard type traits, never from a compiler's
/// own spelling of the type.
///
/// @tparam T The arithmetic type.
/// @return The tag.
template <typename T>
    requires std::is_arithmetic_v<T>
[[nodiscard]] consteval ShapeTag numericShapeTag() {
    ShapeTag tag{};
    if constexpr (std::same_as<T, bool>) {
        tag.append("bool");
    } else {
        if constexpr (std::floating_point<T>) {
            tag.append("f");
        } else if constexpr (std::is_signed_v<T>) {
            tag.append("i");
        } else {
            tag.append("u");
        }
        tag.appendDecimal(sizeof(T) * 8U);
    }
    return tag;
}

/// @brief Storage for a `Ranged`'s `$defs` key.
/// @tparam T The payload's arithmetic type (`decltype(Min)`).
template <typename T>
    requires std::is_arithmetic_v<T>
inline constexpr auto rangedSchemaNameStorage = [] {
    constexpr std::string_view prefix{"Ranged_"};
    constexpr ShapeTag tag = numericShapeTag<T>();

    std::array<char, prefix.size() + tag.size> out{};
    std::size_t at = 0;
    auto append = [&out, &at](std::string_view part) {
        for (char const character : part) {
            out[at] = character;
            ++at;
        }
    };

    append(prefix);
    append(tag.view());
    return out;
}();

/// @brief The `$defs` key for one `Ranged` instantiation.
///
/// Keyed on the payload type alone, deliberately: a `Ranged`'s definition is
/// the schema of `std::optional<decltype(Min)>` and nothing more — the bounds
/// themselves are emitted as property-level `x-min`/`x-max`/`x-step`, never
/// into the `$def`. So `Ranged<0, 100>` and `Ranged<5, 50>` share one entry
/// because their entries *are* the same entry, while `Ranged<0.0, 1.0, 0.1>`
/// gets its own, which is the split morph#543 is about.
///
/// @tparam T The payload's arithmetic type (`decltype(Min)`).
template <typename T>
    requires std::is_arithmetic_v<T>
inline constexpr std::string_view rangedSchemaName{rangedSchemaNameStorage<T>.data(),
                                                   rangedSchemaNameStorage<T>.size()};

}  // namespace morph::forms::detail
