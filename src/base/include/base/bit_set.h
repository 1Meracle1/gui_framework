#pragma once

#include <array>
#include <base/assert.h>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <type_traits>

template <typename Element, size_t COUNT, Element FIRST = Element{}, typename Word = uint64_t>
class BitSet final {
  public:
    static_assert(
        std::is_enum_v<Element> || (std::is_integral_v<Element> && !std::is_same_v<Element, bool>),
        "BitSet element type must be an enum or integer type"
    );
    static_assert(
        std::is_integral_v<Word> && std::is_unsigned_v<Word> && !std::is_same_v<Word, bool>,
        "BitSet word type must be an unsigned integer type"
    );

    using Value = Element;
    using WordType = Word;
    using ElementValue = decltype([] {
        if constexpr (std::is_enum_v<Element>) {
            return static_cast<std::underlying_type_t<Element>>(Element{});
        } else {
            return Element{};
        }
    }());

    static constexpr size_t BIT_COUNT = COUNT;
    static constexpr Element FIRST_VALUE = FIRST;
    static constexpr size_t WORD_BITS = std::numeric_limits<Word>::digits;
    static constexpr size_t WORD_COUNT = (BIT_COUNT + WORD_BITS - 1u) / WORD_BITS;

    constexpr BitSet() = default;

    constexpr BitSet(std::initializer_list<Element> values) {
        add(values);
    }

    [[nodiscard]] static constexpr auto element_count() -> size_t {
        return BIT_COUNT;
    }

    [[nodiscard]] static constexpr auto first_value() -> Element {
        return FIRST_VALUE;
    }

    [[nodiscard]] constexpr auto empty() const -> bool {
        for (Word word : m_words) {
            if (word != Word{0u}) {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] constexpr auto any() const -> bool {
        return !empty();
    }

    [[nodiscard]] constexpr auto all() const -> bool {
        if constexpr (BIT_COUNT == 0u) {
            return true;
        } else {
            for (size_t index = 0u; index < WORD_COUNT - 1u; ++index) {
                if (m_words[index] != all_bits()) {
                    return false;
                }
            }
            return m_words[WORD_COUNT - 1u] == last_word_mask();
        }
    }

    [[nodiscard]] constexpr auto cardinality() const -> size_t {
        size_t result = 0u;
        for (Word word : m_words) {
            result += static_cast<size_t>(std::popcount(word));
        }
        return result;
    }

    [[nodiscard]] constexpr auto count() const -> size_t {
        return cardinality();
    }

    [[nodiscard]] constexpr auto contains(Element value) const -> bool {
        size_t index = 0u;
        if (!index_of(value, &index)) {
            return false;
        }
        return (m_words[word_index(index)] & bit_mask(index)) != Word{0u};
    }

    [[nodiscard]] constexpr auto operator[](Element value) const -> bool {
        return contains(value);
    }

    [[nodiscard]] constexpr auto contains_all(BitSet other) const -> bool {
        return other.is_subset_of(*this);
    }

    [[nodiscard]] constexpr auto contains_any(BitSet other) const -> bool {
        for (size_t index = 0u; index < WORD_COUNT; ++index) {
            if ((m_words[index] & other.m_words[index]) != Word{0u}) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] constexpr auto is_subset_of(BitSet other) const -> bool {
        for (size_t index = 0u; index < WORD_COUNT; ++index) {
            if ((m_words[index] & ~other.m_words[index]) != Word{0u}) {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] constexpr auto is_superset_of(BitSet other) const -> bool {
        return other.is_subset_of(*this);
    }

    constexpr auto add(Element value) -> bool {
        size_t index = 0u;
        if (!index_of(value, &index)) {
            return false;
        }
        m_words[word_index(index)] |= bit_mask(index);
        return true;
    }

    constexpr auto add(std::initializer_list<Element> values) -> void {
        for (Element value : values) {
            add(value);
        }
    }

    constexpr auto remove(Element value) -> bool {
        size_t index = 0u;
        if (!index_of(value, &index)) {
            return false;
        }
        m_words[word_index(index)] &= ~bit_mask(index);
        return true;
    }

    constexpr auto toggle(Element value) -> bool {
        size_t index = 0u;
        if (!index_of(value, &index)) {
            return false;
        }
        m_words[word_index(index)] ^= bit_mask(index);
        return true;
    }

    constexpr auto set(Element value, bool enabled = true) -> bool {
        return enabled ? add(value) : remove(value);
    }

    constexpr auto clear() -> void {
        for (Word& word : m_words) {
            word = Word{0u};
        }
    }

    constexpr auto reset() -> void {
        clear();
    }

    constexpr auto fill() -> void {
        if constexpr (BIT_COUNT > 0u) {
            for (Word& word : m_words) {
                word = all_bits();
            }
            m_words[WORD_COUNT - 1u] &= last_word_mask();
        }
    }

    [[nodiscard]] constexpr auto word(size_t index) const -> Word {
        DEBUG_ASSERT(index < WORD_COUNT);
        return m_words[index];
    }

    [[nodiscard]] constexpr auto words() const -> std::array<Word, WORD_COUNT> const& {
        return m_words;
    }

    [[nodiscard]] friend constexpr auto operator==(BitSet lhs, BitSet rhs) -> bool {
        for (size_t index = 0u; index < WORD_COUNT; ++index) {
            if (lhs.m_words[index] != rhs.m_words[index]) {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] friend constexpr auto operator!=(BitSet lhs, BitSet rhs) -> bool {
        return !(lhs == rhs);
    }

    [[nodiscard]] friend constexpr auto operator<=(BitSet lhs, BitSet rhs) -> bool {
        return lhs.is_subset_of(rhs);
    }

    [[nodiscard]] friend constexpr auto operator<(BitSet lhs, BitSet rhs) -> bool {
        return lhs != rhs && lhs <= rhs;
    }

    [[nodiscard]] friend constexpr auto operator>=(BitSet lhs, BitSet rhs) -> bool {
        return lhs.is_superset_of(rhs);
    }

    [[nodiscard]] friend constexpr auto operator>(BitSet lhs, BitSet rhs) -> bool {
        return lhs != rhs && lhs >= rhs;
    }

    [[nodiscard]] friend constexpr auto operator|(BitSet lhs, BitSet rhs) -> BitSet {
        lhs |= rhs;
        return lhs;
    }

    [[nodiscard]] friend constexpr auto operator+(BitSet lhs, BitSet rhs) -> BitSet {
        lhs |= rhs;
        return lhs;
    }

    [[nodiscard]] friend constexpr auto operator&(BitSet lhs, BitSet rhs) -> BitSet {
        lhs &= rhs;
        return lhs;
    }

    [[nodiscard]] friend constexpr auto operator-(BitSet lhs, BitSet rhs) -> BitSet {
        lhs -= rhs;
        return lhs;
    }

    [[nodiscard]] friend constexpr auto operator^(BitSet lhs, BitSet rhs) -> BitSet {
        lhs ^= rhs;
        return lhs;
    }

    [[nodiscard]] friend constexpr auto operator~(BitSet value) -> BitSet {
        for (Word& word : value.m_words) {
            word = ~word;
        }
        value.clear_padding_bits();
        return value;
    }

    constexpr auto operator|=(BitSet rhs) -> BitSet& {
        for (size_t index = 0u; index < WORD_COUNT; ++index) {
            m_words[index] |= rhs.m_words[index];
        }
        return *this;
    }

    constexpr auto operator+=(BitSet rhs) -> BitSet& {
        return *this |= rhs;
    }

    constexpr auto operator&=(BitSet rhs) -> BitSet& {
        for (size_t index = 0u; index < WORD_COUNT; ++index) {
            m_words[index] &= rhs.m_words[index];
        }
        return *this;
    }

    constexpr auto operator-=(BitSet rhs) -> BitSet& {
        for (size_t index = 0u; index < WORD_COUNT; ++index) {
            m_words[index] &= ~rhs.m_words[index];
        }
        return *this;
    }

    constexpr auto operator^=(BitSet rhs) -> BitSet& {
        for (size_t index = 0u; index < WORD_COUNT; ++index) {
            m_words[index] ^= rhs.m_words[index];
        }
        return *this;
    }

  private:
    [[nodiscard]] static constexpr auto all_bits() -> Word {
        return ~Word{0u};
    }

    [[nodiscard]] static constexpr auto last_word_bit_count() -> size_t {
        size_t const remainder = BIT_COUNT % WORD_BITS;
        return remainder == 0u ? WORD_BITS : remainder;
    }

    [[nodiscard]] static constexpr auto last_word_mask() -> Word {
        if constexpr (BIT_COUNT == 0u) {
            return Word{0u};
        } else if constexpr (last_word_bit_count() == WORD_BITS) {
            return all_bits();
        } else {
            return static_cast<Word>((Word{1u} << last_word_bit_count()) - Word{1u});
        }
    }

    [[nodiscard]] static constexpr auto word_index(size_t bit_index) -> size_t {
        return bit_index / WORD_BITS;
    }

    [[nodiscard]] static constexpr auto bit_mask(size_t bit_index) -> Word {
        return static_cast<Word>(Word{1u} << (bit_index % WORD_BITS));
    }

    [[nodiscard]] static constexpr auto index_of(Element value, size_t* out_index) -> bool {
        using SignedValue = std::make_signed_t<ElementValue>;
        using UnsignedValue = std::make_unsigned_t<ElementValue>;

        if constexpr (std::is_signed_v<ElementValue>) {
            SignedValue const raw_value = static_cast<SignedValue>(value);
            SignedValue const raw_first = static_cast<SignedValue>(FIRST_VALUE);
            if (raw_value < raw_first) {
                return false;
            }
            UnsignedValue const offset = static_cast<UnsignedValue>(raw_value - raw_first);
            if (offset >= BIT_COUNT) {
                return false;
            }
            *out_index = static_cast<size_t>(offset);
        } else {
            UnsignedValue const raw_value = static_cast<UnsignedValue>(value);
            UnsignedValue const raw_first = static_cast<UnsignedValue>(FIRST_VALUE);
            if (raw_value < raw_first) {
                return false;
            }
            UnsignedValue const offset = raw_value - raw_first;
            if (offset >= BIT_COUNT) {
                return false;
            }
            *out_index = static_cast<size_t>(offset);
        }
        return true;
    }

    constexpr auto clear_padding_bits() -> void {
        if constexpr (BIT_COUNT > 0u) {
            m_words[WORD_COUNT - 1u] &= last_word_mask();
        }
    }

  private:
    std::array<Word, WORD_COUNT> m_words = {};
};

template <typename Element, size_t COUNT, Element FIRST, typename Word>
[[nodiscard]] constexpr auto card(BitSet<Element, COUNT, FIRST, Word> value) -> size_t {
    return value.cardinality();
}
