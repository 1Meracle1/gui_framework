#include <base/assert.h>
#include <base/bit_set.h>
#include <base/crash.h>
#include <base/fmt.h>
#include <base/hash_map.h>
#include <base/io.h>
#include <base/memory.h>
#include <base/small_array.h>
#include <base/str_ref.h>
#include <base/string_buffer.h>
#include <base/xar.h>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory_resource>
#include <string>
#include <string_view>
#include <test/test.h>
#include <utility>
#include <vector>

namespace {

    struct CapturedAssert {
        char const* expression;
        char const* file;
        uint32_t line;
        uint32_t count;
    };

    CapturedAssert captured_assert = {};

    auto capture_assert_handler(char const* expression, char const* file, uint32_t line) -> void {
        captured_assert.expression = expression;
        captured_assert.file = file;
        captured_assert.line = line;
        captured_assert.count += 1u;
    }

    static_assert(StrRef("framework").substr(5u, 4u) == "work");
    static_assert(StrRef("FRAMEWORK").equals_ignore_ascii_case("framework"));
    static_assert(StrRef("  text\t").trim() == "text");

    constexpr auto make_constexpr_small_array() -> SmallArray<int, 4u> {
        SmallArray<int, 4u> values;
        BASE_UNUSED(values.push_back(10));
        BASE_UNUSED(values.push_back(20));
        values.consume(1u);
        BASE_UNUSED(values.inject_at(15, 1u));
        return values;
    }

    static_assert(make_constexpr_small_array().len == 2u);
    static_assert(make_constexpr_small_array().data[0u] == 10);
    static_assert(make_constexpr_small_array().data[1u] == 15);

    enum class Direction : uint8_t {
        NORTH = 1u,
        EAST = 2u,
        SOUTH = 3u,
        WEST = 4u,
    };

    using DirectionSet = BitSet<Direction, 4u, Direction::NORTH, uint8_t>;
    using UppercaseSet = BitSet<char, 26u, 'A'>;

    constexpr auto make_constexpr_bit_set() -> DirectionSet {
        DirectionSet values;
        BASE_UNUSED(values.add(Direction::NORTH));
        BASE_UNUSED(values.add(Direction::WEST));
        BASE_UNUSED(values.toggle(Direction::NORTH));
        return values;
    }

    static_assert(make_constexpr_bit_set().contains(Direction::WEST));
    static_assert(!make_constexpr_bit_set().contains(Direction::NORTH));
    static_assert(card(make_constexpr_bit_set()) == 1u);
    static_assert(sizeof(UppercaseSet) == sizeof(uint64_t));

    auto expect_file_text(test::Context* context, std::FILE* file, StrRef expected) -> bool {
        char buffer[256] = {};
        bool const positioned = std::fseek(file, 0, SEEK_SET) == 0;
        TEST_EXPECT(context, positioned);

        if (!positioned) {
            return false;
        }

        size_t const read_size = std::fread(buffer, 1u, sizeof(buffer), file);

        TEST_EXPECT(context, read_size == expected.size());
        return TEST_EXPECT(context, StrRef(buffer, read_size) == expected);
    }

    [[nodiscard]] auto open_temp_file() -> std::FILE* {
#if defined(_MSC_VER)
        std::FILE* file = nullptr;
        return tmpfile_s(&file) == 0 ? file : nullptr;
#else
        return std::tmpfile();
#endif
    }

    struct CollidingIntHasher {
        [[nodiscard]] auto operator()(int const& key, uintptr_t seed) const -> uintptr_t {
            BASE_UNUSED(seed);
            return (static_cast<uintptr_t>(key) & 3u) + 1u;
        }
    };

    TEST_CASE(crash_reason_names_are_stable) {
        TEST_EXPECT(context,
                    StrRef(base::crash_reason_name(base::CrashReason::ASSERTION_FAILURE)) ==
                        "assertion failed");
        TEST_EXPECT(context, StrRef(base::crash_reason_name(base::CrashReason::PANIC)) == "panic");
        TEST_EXPECT(context,
                    StrRef(base::crash_reason_name(base::CrashReason::UNREACHABLE_CODE)) ==
                        "unreachable code reached");
        TEST_EXPECT(context,
                    StrRef(base::crash_reason_name(base::CrashReason::PROCESS_FAULT)) ==
                        "process fault");
    }

    TEST_CASE(assert_handler_intercepts_assertions) {
        captured_assert = {};
        base::set_assert_handler(capture_assert_handler);
        uint32_t const assert_line = __LINE__ + 1u;
        ASSERT_MSG(false, "captured by test");
        base::set_assert_handler(nullptr);

        TEST_EXPECT(context, captured_assert.count == 1u);
        TEST_EXPECT(context, StrRef(captured_assert.expression) == "false");
        TEST_EXPECT(context, StrRef(captured_assert.file) == __FILE__);
        TEST_EXPECT(context, captured_assert.line == assert_line);
    }

    TEST_CASE(arena_allocates_aligned_memory_from_virtual_memory) {
        Arena arena;
        ArenaOptions options = {};
        options.reserve_size = 256u * 1024u;
        options.commit_size = 64u * 1024u;

        TEST_EXPECT(context, arena.init(options));

        void* const first = arena.allocate_bytes(13u, 8u);
        void* const second = arena.allocate_bytes(32u, 32u);

        TEST_EXPECT(context, first != nullptr);
        TEST_EXPECT(context, second != nullptr);
        TEST_EXPECT(context, (reinterpret_cast<uintptr_t>(first) % 8u) == 0u);
        TEST_EXPECT(context, (reinterpret_cast<uintptr_t>(second) % 32u) == 0u);
        TEST_EXPECT(context, arena.used_size() >= 45u);
        TEST_EXPECT(context, arena.committed_size() >= arena.used_size());
        TEST_EXPECT(context, arena.reserved_size() >= arena.committed_size());
    }

    TEST_CASE(arena_markers_and_reset_reuse_linear_storage) {
        Arena arena;
        ArenaOptions options = {};
        options.reserve_size = 256u * 1024u;
        options.commit_size = 64u * 1024u;

        TEST_EXPECT(context, arena.init(options));

        void* const first = arena.allocate_bytes(64u, 16u);
        ArenaMarker const marker = arena.marker();
        void* const second = arena.allocate_bytes(64u, 16u);

        arena.reset_to(marker);
        void* const second_again = arena.allocate_bytes(64u, 16u);

        arena.reset();
        void* const first_again = arena.allocate_bytes(64u, 16u);

        TEST_EXPECT(context, second_again == second);
        TEST_EXPECT(context, first_again == first);
    }

    TEST_CASE(arena_resource_interoperates_with_pmr_containers) {
        Arena arena;
        ArenaOptions options = {};
        options.reserve_size = 256u * 1024u;
        options.commit_size = 64u * 1024u;

        TEST_EXPECT(context, arena.init(options));

        std::pmr::vector<int> values(arena.resource());
        values.push_back(10);
        values.push_back(20);
        values.push_back(30);

        TEST_EXPECT(context, values.size() == 3u);
        TEST_EXPECT(context, values[0] == 10);
        TEST_EXPECT(context, values[1] == 20);
        TEST_EXPECT(context, values[2] == 30);
        TEST_EXPECT(context, arena.used_size() != 0u);
    }

    TEST_CASE(arena_temp_scope_rolls_back_to_marker) {
        Arena arena;
        ArenaOptions options = {};
        options.reserve_size = 256u * 1024u;
        options.commit_size = 64u * 1024u;

        TEST_EXPECT(context, arena.init(options));

        void* const stable = arena.allocate_bytes(32u, 8u);
        size_t const used_before_temp = arena.used_size();
        void* transient = nullptr;

        {
            ArenaTemp temp(arena);
            TEST_EXPECT(context, temp.arena() == &arena);
            transient = arena.allocate_bytes(128u, 8u);
            TEST_EXPECT(context, arena.used_size() > used_before_temp);
        }

        TEST_EXPECT(context, arena.used_size() == used_before_temp);

        void* const reused = arena.allocate_bytes(128u, 8u);

        TEST_EXPECT(context, stable != nullptr);
        TEST_EXPECT(context, reused == transient);
        TEST_EXPECT(context, arena.used_size() == used_before_temp + 128u);
    }

    TEST_CASE(small_array_pushes_pops_and_clamps_resize_to_capacity) {
        SmallArray<int, 3u> values;

        TEST_EXPECT(context, values.empty());
        TEST_EXPECT(context, values.capacity() == 3u);
        TEST_EXPECT(context, values.space() == 3u);
        TEST_EXPECT(context, values.push_back(1));
        TEST_EXPECT(context, values.push_back(2));
        TEST_EXPECT(context, values.push_front(0));
        TEST_EXPECT(context, !values.push_back(3));
        TEST_EXPECT(context, values.full());
        TEST_EXPECT(context, values.len == 3u);
        TEST_EXPECT(context, values.data[0u] == 0);
        TEST_EXPECT(context, values.data[1u] == 1);
        TEST_EXPECT(context, values.data[2u] == 2);

        TEST_EXPECT(context, values.pop_front() == 0);
        TEST_EXPECT(context, values.pop_back() == 2);
        TEST_EXPECT(context, values.len == 1u);
        TEST_EXPECT(context, values.data[0u] == 1);

        values.resize(5u);
        values[2u] = 7;
        SmallArray<int, 3u> const& const_values = values;

        TEST_EXPECT(context, values.len == 3u);
        TEST_EXPECT(context, values.data[0u] == 1);
        TEST_EXPECT(context, values.data[1u] == 0);
        TEST_EXPECT(context, const_values[2u] == 7);
    }

    TEST_CASE(small_array_safe_access_and_bulk_append_report_failure_without_mutating) {
        SmallArray<int, 4u> values;
        int more_values[] = {2, 3, 4};

        TEST_EXPECT(context, values.push_back(1));
        TEST_EXPECT(context, values.push_back_elems(more_values));
        TEST_EXPECT(context, values.len == 4u);
        TEST_EXPECT(context, !values.push_back_elems({5, 6}));
        TEST_EXPECT(context, values.len == 4u);

        SmallArrayResult<int> const found = values.get_safe(2u);
        SmallArrayResult<int> const missing = values.get_safe(4u);
        SmallArrayPtrResult<int> const found_ptr = values.get_ptr_safe(1u);

        TEST_EXPECT(context, found);
        TEST_EXPECT(context, found.value == 3);
        TEST_EXPECT(context, !missing);
        TEST_EXPECT(context, found_ptr);
        TEST_EXPECT(context, found_ptr.ptr != nullptr && *found_ptr.ptr == 2);
    }

    TEST_CASE(small_array_insert_remove_clear_and_zero_capacity_behave_like_odin_container) {
        SmallArray<int, 5u> values;

        TEST_EXPECT(context, values.append({0, 1, 2, 3}));
        TEST_EXPECT(context, values.inject_at(9, 2u));
        TEST_EXPECT(context, values.len == 5u);
        TEST_EXPECT(context, values.data[0u] == 0);
        TEST_EXPECT(context, values.data[1u] == 1);
        TEST_EXPECT(context, values.data[2u] == 9);
        TEST_EXPECT(context, values.data[3u] == 2);
        TEST_EXPECT(context, values.data[4u] == 3);

        values.ordered_remove(1u);

        TEST_EXPECT(context, values.len == 4u);
        TEST_EXPECT(context, values.data[0u] == 0);
        TEST_EXPECT(context, values.data[1u] == 9);
        TEST_EXPECT(context, values.data[2u] == 2);
        TEST_EXPECT(context, values.data[3u] == 3);

        values.unordered_remove(1u);

        TEST_EXPECT(context, values.len == 3u);
        TEST_EXPECT(context, values.data[0u] == 0);
        TEST_EXPECT(context, values.data[1u] == 3);
        TEST_EXPECT(context, values.data[2u] == 2);

        values.clear();

        TEST_EXPECT(context, values.empty());
        TEST_EXPECT(context, !values.pop_back_safe());
        TEST_EXPECT(context, !values.pop_front_safe());

        SmallArray<int, 0u> zero_capacity;

        TEST_EXPECT(context, zero_capacity.capacity() == 0u);
        TEST_EXPECT(context, zero_capacity.space() == 0u);
        TEST_EXPECT(context, !zero_capacity.push_back(1));
        TEST_EXPECT(context, !zero_capacity.inject_at(1, 0u));
        TEST_EXPECT(context, zero_capacity.slice().empty());
    }

    TEST_CASE(bit_set_models_odin_style_set_operations) {
        DirectionSet north_west = {Direction::NORTH, Direction::WEST};
        DirectionSet south_west = {Direction::SOUTH, Direction::WEST};

        TEST_EXPECT(context, north_west.contains(Direction::NORTH));
        TEST_EXPECT(context, north_west[Direction::WEST]);
        TEST_EXPECT(context, !north_west.contains(Direction::EAST));
        TEST_EXPECT(context, north_west.word(0u) == 0b1001u);
        TEST_EXPECT(context, card(north_west) == 2u);

        DirectionSet const combined = north_west | south_west;
        DirectionSet const plus_combined = north_west + south_west;
        DirectionSet const intersection = north_west & south_west;
        DirectionSet const difference = combined - north_west;
        DirectionSet const symmetric = north_west ^ south_west;
        DirectionSet const west = {Direction::WEST};
        DirectionSet const south = {Direction::SOUTH};
        DirectionSet const north_south = {Direction::NORTH, Direction::SOUTH};

        TEST_EXPECT(context, combined == plus_combined);
        TEST_EXPECT(context, combined.contains(Direction::NORTH));
        TEST_EXPECT(context, combined.contains(Direction::SOUTH));
        TEST_EXPECT(context, combined.contains(Direction::WEST));
        TEST_EXPECT(context, card(combined) == 3u);

        TEST_EXPECT(context, intersection == west);
        TEST_EXPECT(context, difference == south);
        TEST_EXPECT(context, symmetric == north_south);

        TEST_EXPECT(context, north_west <= combined);
        TEST_EXPECT(context, north_west < combined);
        TEST_EXPECT(context, combined >= north_west);
        TEST_EXPECT(context, combined > north_west);
        TEST_EXPECT(context, combined.contains_all(north_west));
        TEST_EXPECT(context, north_west.contains_any(south_west));
    }

    TEST_CASE(bit_set_supports_ranges_membership_and_cardinality) {
        UppercaseSet letters = {'A', 'B', 'Y'};

        TEST_EXPECT(context, letters.contains('A'));
        TEST_EXPECT(context, letters.contains('Y'));
        TEST_EXPECT(context, !letters.contains('Z'));
        TEST_EXPECT(context, !letters.add('@'));
        TEST_EXPECT(context, !letters.add('['));
        TEST_EXPECT(context, letters.add('Z'));
        TEST_EXPECT(context, letters.remove('B'));
        TEST_EXPECT(context, !letters.contains('B'));
        TEST_EXPECT(context, card(letters) == 3u);

        UppercaseSet all_letters;
        all_letters.fill();

        TEST_EXPECT(context, all_letters.all());
        TEST_EXPECT(context, letters < all_letters);
        TEST_EXPECT(context, (~letters).contains('B'));
        TEST_EXPECT(context, !(~letters).contains('A'));

        letters.clear();

        TEST_EXPECT(context, letters.empty());
    }

    TEST_CASE(xar_array_grows_in_exponential_chunks_and_keeps_stable_addresses) {
        XarArray<int, 2u> values;

        TEST_EXPECT(context, values.init());
        TEST_EXPECT(context, values.empty());

        int* const first = values.push_back_and_get_ptr(10);
        int* const second = values.push_back_and_get_ptr(20);

        TEST_EXPECT(context, first != nullptr);
        TEST_EXPECT(context, second != nullptr);
        TEST_EXPECT(context, values.capacity() == 4u);

        for (int value = 30; value < 180; value += 10) {
            TEST_EXPECT(context, values.push_back(value));
        }

        TEST_EXPECT(context, values.len() == 17u);
        TEST_EXPECT(context, values.capacity() == 32u);
        TEST_EXPECT(context, first == values.get_ptr(0u));
        TEST_EXPECT(context, second == values.get_ptr(1u));
        TEST_EXPECT(context, first != nullptr && *first == 10);
        TEST_EXPECT(context, second != nullptr && *second == 20);
        TEST_EXPECT(context, values.get(16u) == 170);

        int sum = 0;
        for (int const value : values) {
            sum += value;
        }

        TEST_EXPECT(context, sum == 1530);
    }

    TEST_CASE(xar_array_safe_access_pop_and_unordered_remove_match_odin_container) {
        XarArray<int, 3u> values;

        TEST_EXPECT(context, values.append({1, 2, 3, 4}) == 4u);

        XarResult<int> const found = values.get_safe(2u);
        XarResult<int> const missing = values.get_safe(9u);

        TEST_EXPECT(context, found);
        TEST_EXPECT(context, found.value == 3);
        TEST_EXPECT(context, !missing);

        values.unordered_remove(1u);

        TEST_EXPECT(context, values.len() == 3u);
        TEST_EXPECT(context, values.get(0u) == 1);
        TEST_EXPECT(context, values.get(1u) == 4);
        TEST_EXPECT(context, values.get(2u) == 3);

        TEST_EXPECT(context, values.pop() == 3);
        TEST_EXPECT(context, values.pop_safe().value == 4);
        TEST_EXPECT(context, values.pop_safe().value == 1);
        TEST_EXPECT(context, !values.pop_safe());
    }

    struct XarFreelistValue {
        uintptr_t id;
        uintptr_t payload;

        [[nodiscard]] friend auto operator==(XarFreelistValue const& lhs,
                                             XarFreelistValue const& rhs) -> bool {
            return lhs.id == rhs.id && lhs.payload == rhs.payload;
        }
    };

    TEST_CASE(xar_freelist_reuses_released_slots_and_skips_them_when_iterating) {
        XarFreelistArray<XarFreelistValue, 2u> values;

        TEST_EXPECT(context, values.init());

        XarPushResult<XarFreelistValue> const first = values.push_with_index({1u, 10u});
        XarPushResult<XarFreelistValue> const second = values.push_with_index({2u, 20u});
        XarPushResult<XarFreelistValue> const third = values.push_with_index({3u, 30u});

        TEST_EXPECT(context, first);
        TEST_EXPECT(context, second);
        TEST_EXPECT(context, third);
        TEST_EXPECT(context, first.index == 0u);
        TEST_EXPECT(context, second.index == 1u);
        TEST_EXPECT(context, third.index == 2u);

        XarFreelistValue const removed = values.pop(1u);

        TEST_EXPECT(context, removed.id == 2u);
        TEST_EXPECT(context, values.is_freed(1u));

        size_t iterated_count = 0u;
        uintptr_t id_sum = 0u;
        for (XarFreelistValue const& value : values) {
            iterated_count += 1u;
            id_sum += value.id;
        }

        TEST_EXPECT(context, iterated_count == 2u);
        TEST_EXPECT(context, id_sum == 4u);

        XarPushResult<XarFreelistValue> const reused = values.push_with_index({4u, 40u});

        TEST_EXPECT(context, reused);
        TEST_EXPECT(context, reused.index == 1u);
        TEST_EXPECT(context, reused.ptr == second.ptr);
        TEST_EXPECT(context, values.get(1u).id == 4u);
        TEST_EXPECT(context, !values.is_freed(1u));
    }

    TEST_CASE(thread_temp_arenas_reset_each_frame) {
        shutdown_thread_temp_arenas();

        ThreadTempArenaOptions options = {};
        options.reserve_size = 256u * 1024u;
        options.commit_size = 64u * 1024u;

        bool const initialized = init_thread_temp_arenas(options);
        TEST_EXPECT(context, initialized);

        if (!initialized) {
            return;
        }

        Arena& arena = thread_temp_arena();
        void* const first = arena.allocate_bytes(64u, 16u);

        TEST_EXPECT(context, arena.used_size() != 0u);

        reset_thread_temp_arenas();

        TEST_EXPECT(context, arena.used_size() == 0u);

        void* const first_again = arena.allocate_bytes(64u, 16u);

        TEST_EXPECT(context, first_again == first);

        {
            ArenaTemp temp = begin_thread_temp_arena();
            TEST_EXPECT(context, temp.arena() == &arena);
            BASE_UNUSED(arena.allocate_bytes(32u, 8u));
        }

        TEST_EXPECT(context, arena.used_size() == 64u);

        shutdown_thread_temp_arenas();
    }

    TEST_CASE(hash_map_sets_gets_updates_and_iterates_entries) {
        HashMap<int, int> map;

        TEST_EXPECT(context, map.init());
        TEST_EXPECT(context, map.empty());
        TEST_EXPECT(context, map.set(10, 100) != nullptr);
        TEST_EXPECT(context, map.set(20, 200) != nullptr);
        TEST_EXPECT(context, map.set(10, 150) != nullptr);

        int* const ten = map.get(10);
        int* const twenty = map.get(20);

        TEST_EXPECT(context, ten != nullptr);
        TEST_EXPECT(context, twenty != nullptr);
        TEST_EXPECT(context, ten != nullptr && *ten == 150);
        TEST_EXPECT(context, twenty != nullptr && *twenty == 200);
        TEST_EXPECT(context, map.get(30) == nullptr);
        TEST_EXPECT(context, map.size() == 2u);
        TEST_EXPECT(context, map.capacity() >= 8u);

        int key_sum = 0;
        int value_sum = 0;
        size_t entry_count = 0u;

        for (HashMap<int, int>::Entry entry : map) {
            key_sum += *entry.key;
            value_sum += *entry.value;
            entry_count += 1u;
        }

        TEST_EXPECT(context, entry_count == 2u);
        TEST_EXPECT(context, key_sum == 30);
        TEST_EXPECT(context, value_sum == 350);
    }

    TEST_CASE(hash_map_grows_and_preserves_entries) {
        HashMap<int, int> map;

        TEST_EXPECT(context, map.init());

        for (int value = 0; value < 200; ++value) {
            TEST_EXPECT(context, map.set(value, value * 10) != nullptr);
        }

        TEST_EXPECT(context, map.size() == 200u);
        TEST_EXPECT(context, map.capacity() >= 256u);

        for (int value = 0; value < 200; ++value) {
            int const* const found = map.get(value);
            TEST_EXPECT(context, found != nullptr);
            TEST_EXPECT(context, found != nullptr && *found == value * 10);
        }
    }

    TEST_CASE(hash_map_uses_str_ref_content_for_default_hashing) {
        char first_key[] = "panel";
        char equivalent_key[] = "panel";
        char other_key[] = "dock";
        HashMap<StrRef, int> map;

        TEST_EXPECT(context, map.init());
        TEST_EXPECT(context, map.set(StrRef(first_key), 7) != nullptr);
        TEST_EXPECT(context, map.set(StrRef(other_key), 3) != nullptr);

        int const* const found = map.get(StrRef(equivalent_key));
        int const* const other = map.get(StrRef("dock"));

        TEST_EXPECT(context, found != nullptr);
        TEST_EXPECT(context, found != nullptr && *found == 7);
        TEST_EXPECT(context, other != nullptr);
        TEST_EXPECT(context, other != nullptr && *other == 3);
    }

    TEST_CASE(hash_map_erases_tombstones_and_reuses_collision_slots) {
        HashMap<int, int, CollidingIntHasher> map;

        TEST_EXPECT(context, map.init());

        for (int value = 0; value < 32; ++value) {
            TEST_EXPECT(context, map.set(value, value + 1000) != nullptr);
        }

        for (int value = 0; value < 32; value += 3) {
            int old_key = -1;
            int old_value = -1;
            TEST_EXPECT(context, map.erase(value, &old_key, &old_value));
            TEST_EXPECT(context, old_key == value);
            TEST_EXPECT(context, old_value == value + 1000);
        }

        for (int value = 0; value < 32; ++value) {
            int const* const found = map.get(value);
            if ((value % 3) == 0) {
                TEST_EXPECT(context, found == nullptr);
            } else {
                TEST_EXPECT(context, found != nullptr);
                TEST_EXPECT(context, found != nullptr && *found == value + 1000);
            }
        }

        for (int value = 100; value < 116; ++value) {
            TEST_EXPECT(context, map.set(value, value + 2000) != nullptr);
        }

        for (int value = 100; value < 116; ++value) {
            int const* const found = map.get(value);
            TEST_EXPECT(context, found != nullptr);
            TEST_EXPECT(context, found != nullptr && *found == value + 2000);
        }
    }

    TEST_CASE(string_view_constructs_from_common_string_sources) {
        std::string owned = "owned";
        StrRef from_string = owned;

        TEST_EXPECT(context, from_string == "owned");
        TEST_EXPECT(context, from_string.data() == owned.data());

        std::string_view std_view = "standard view";
        StrRef from_std_view = std_view;

        TEST_EXPECT(context, from_std_view == "standard view");
        TEST_EXPECT(context, from_std_view.to_string_view() == std_view);

        char mutable_text[] = "mutable";
        StrRef from_mutable_text = mutable_text;

        TEST_EXPECT(context, from_mutable_text == "mutable");
        mutable_text[0] = 'M';
        TEST_EXPECT(context, from_mutable_text.starts_with('M'));

        uint8_t raw_bytes[] = {'b', 'y', 't', 'e'};
        StrRef from_raw_bytes(raw_bytes, sizeof(raw_bytes));

        TEST_EXPECT(context, from_raw_bytes == "byte");
        TEST_EXPECT(context, from_raw_bytes.size() == 4u);
        TEST_EXPECT(context, from_raw_bytes.bytes().size() == 4u);

        char8_t utf8_text[] = u8"utf8";
        StrRef from_utf8(utf8_text);

        TEST_EXPECT(context, from_utf8 == "utf8");

        char buffer[8] = {};
        size_t const copied_size = from_raw_bytes.copy_to(buffer, sizeof(buffer));

        TEST_EXPECT(context, copied_size == 4u);
        TEST_EXPECT(context, StrRef(buffer, copied_size) == "byte");
    }

    TEST_CASE(string_view_search_slice_and_trim_operations) {
        StrRef text = "  /assets/button.png  ";
        StrRef path = text.trim();

        TEST_EXPECT(context, path == "/assets/button.png");
        TEST_EXPECT(context, path.starts_with('/'));
        TEST_EXPECT(context, path.starts_with("/assets"));
        TEST_EXPECT(context, path.ends_with(".png"));
        TEST_EXPECT(context, path.contains("button"));
        TEST_EXPECT(context, path.find("button") == 8u);
        TEST_EXPECT(context, path.rfind('/') == 7u);
        TEST_EXPECT(context, path.substr(8u, 6u) == "button");
        TEST_EXPECT(context, path.prefix(7u) == "/assets");
        TEST_EXPECT(context, path.suffix(4u) == ".png");
        TEST_EXPECT(context, path.drop_prefix(8u) == "button.png");
        TEST_EXPECT(context, path.drop_suffix(4u) == "/assets/button");
        TEST_EXPECT(context, path.count('/') == 2u);
        TEST_EXPECT(context, path.count("tt") == 1u);

        StrRef stripped;

        TEST_EXPECT(context, path.strip_prefix("/assets/", &stripped));
        TEST_EXPECT(context, stripped == "button.png");
        TEST_EXPECT(context, path.strip_suffix(".png", &stripped));
        TEST_EXPECT(context, stripped == "/assets/button");
        TEST_EXPECT(context, !path.strip_prefix("/missing", &stripped));
        TEST_EXPECT(context, StrRef("---name---").trim_matches('-') == "name");
        TEST_EXPECT(context, StrRef("Window").equals_ignore_ascii_case("window"));
        TEST_EXPECT(context, StrRef("abc123").is_ascii_alphanumeric_only());
        TEST_EXPECT(context, StrRef(" \n\t").is_ascii_whitespace_only());
    }

    TEST_CASE(string_view_split_operations) {
        StrRef csv = "red,green,blue,";
        StrRef csv_parts[4] = {};
        size_t csv_part_count = 0u;

        for (StrRef part : csv.split(",")) {
            if (csv_part_count < 4u) {
                csv_parts[csv_part_count] = part;
            }

            ++csv_part_count;
        }

        TEST_EXPECT(context, csv_part_count == 4u);
        TEST_EXPECT(context, csv_parts[0] == "red");
        TEST_EXPECT(context, csv_parts[1] == "green");
        TEST_EXPECT(context, csv_parts[2] == "blue");
        TEST_EXPECT(context, csv_parts[3].empty());

        StrRef::SplitOnce namespace_split = StrRef("gui::button").split_once("::");

        TEST_EXPECT(context, namespace_split);
        TEST_EXPECT(context, namespace_split.before == "gui");
        TEST_EXPECT(context, namespace_split.after == "button");

        StrRef::SplitOnce path_split = StrRef("/assets/button.png").rsplit_once('/');

        TEST_EXPECT(context, path_split);
        TEST_EXPECT(context, path_split.before == "/assets");
        TEST_EXPECT(context, path_split.after == "button.png");

        StrRef lines = "first\r\nsecond\n";
        StrRef line_parts[2] = {};
        size_t line_count = 0u;

        for (StrRef line : lines.lines()) {
            if (line_count < 2u) {
                line_parts[line_count] = line;
            }

            ++line_count;
        }

        TEST_EXPECT(context, line_count == 2u);
        TEST_EXPECT(context, line_parts[0] == "first");
        TEST_EXPECT(context, line_parts[1] == "second");

        StrRef words = "  alpha\tbeta\n\n gamma  ";
        StrRef word_parts[3] = {};
        size_t word_count = 0u;

        for (StrRef word : words.split_ascii_whitespace()) {
            if (word_count < 3u) {
                word_parts[word_count] = word;
            }

            ++word_count;
        }

        TEST_EXPECT(context, word_count == 3u);
        TEST_EXPECT(context, word_parts[0] == "alpha");
        TEST_EXPECT(context, word_parts[1] == "beta");
        TEST_EXPECT(context, word_parts[2] == "gamma");
    }

    TEST_CASE(string_buffer_writes_and_views_dynamic_text) {
        StringBuffer buffer;

        TEST_EXPECT(context, buffer.write_string("gui") == 3u);
        TEST_EXPECT(context, buffer.write_byte('_') == 1u);
        TEST_EXPECT(context, buffer.write_fill('x', 3u) == 3u);

        TEST_EXPECT(context, buffer.str() == "gui_xxx");
        TEST_EXPECT(context, buffer.size() == 7u);
        TEST_EXPECT(context, buffer.capacity() >= buffer.size());
        TEST_EXPECT(context, buffer.space() == buffer.capacity() - buffer.size());

        char const* const c_text = buffer.c_str();

        TEST_EXPECT(context, c_text != nullptr);
        TEST_EXPECT(context, StrRef(c_text) == "gui_xxx");
        TEST_EXPECT(context, buffer.pop_byte() == 'x');

        TEST_EXPECT(context, buffer.resize(4u));

        TEST_EXPECT(context, buffer.str() == "gui_");

        buffer.reset();

        TEST_EXPECT(context, buffer.empty());
        TEST_EXPECT(context, buffer.str().empty());
    }

    TEST_CASE(string_buffer_reserves_resizes_and_keeps_c_string_space) {
        StringBuffer buffer;

        TEST_EXPECT(context, buffer.init(2u));
        TEST_EXPECT(context, buffer.capacity() == 2u);
        TEST_EXPECT(context, buffer.write_string("ab") == 2u);
        TEST_EXPECT(context, buffer.size() == buffer.capacity());
        TEST_EXPECT(context, buffer.c_str() != nullptr);
        TEST_EXPECT(context, StrRef(buffer.c_str()) == "ab");

        TEST_EXPECT(context, buffer.write_byte('c') == 1u);
        TEST_EXPECT(context, buffer.capacity() >= 3u);
        TEST_EXPECT(context, buffer.str() == "abc");
        TEST_EXPECT(context, buffer.resize(6u, '.'));
        TEST_EXPECT(context, buffer.str() == "abc...");
        TEST_EXPECT(context, buffer.resize(2u));
        TEST_EXPECT(context, buffer.str() == "ab");
    }

    TEST_CASE(string_buffer_uses_fixed_backing_without_growing) {
        char backing[5] = {};
        StringBuffer buffer;

        buffer.init_with_backing(backing, sizeof(backing));
        TEST_EXPECT(context, buffer.capacity() == sizeof(backing));
        TEST_EXPECT(context, buffer.write_string("abcdef") == sizeof(backing));
        TEST_EXPECT(context, buffer.str() == "abcde");
        TEST_EXPECT(context, buffer.c_str() == nullptr);
        TEST_EXPECT(context, buffer.write_byte('!') == 0u);
        TEST_EXPECT(context, buffer.pop_byte() == 'e');
        TEST_EXPECT(context, buffer.c_str() != nullptr);
        TEST_EXPECT(context, StrRef(buffer.c_str()) == "abcd");
    }

    TEST_CASE(string_buffer_self_appends_across_growth_and_moves) {
        StringBuffer buffer;

        TEST_EXPECT(context, buffer.init(4u));
        TEST_EXPECT(context, buffer.write_string("abcd") == 4u);

        StrRef const original = buffer.str();

        TEST_EXPECT(context, buffer.write_string(original) == original.size());
        TEST_EXPECT(context, buffer.str() == "abcdabcd");

        StringBuffer moved = std::move(buffer);

        TEST_EXPECT(context, moved.str() == "abcdabcd");
        TEST_EXPECT(context, buffer.empty());
        TEST_EXPECT(context, buffer.data() == nullptr);
    }

    TEST_CASE(string_buffer_init_replaces_existing_storage) {
        StringBuffer buffer;

        TEST_EXPECT(context, buffer.init(4u));
        TEST_EXPECT(context, buffer.write_string("dynamic") == 7u);
        TEST_EXPECT(context, buffer.capacity() >= 7u);

        char backing[4] = {};
        buffer.init_with_backing(backing, sizeof(backing));

        TEST_EXPECT(context, buffer.empty());
        TEST_EXPECT(context, buffer.capacity() == sizeof(backing));
        TEST_EXPECT(context, buffer.write_string("fixed") == sizeof(backing));
        TEST_EXPECT(context, buffer.str() == "fixe");

        TEST_EXPECT(context, buffer.init(2u));
        TEST_EXPECT(context, buffer.empty());
        TEST_EXPECT(context, buffer.capacity() == 2u);
        TEST_EXPECT(context, buffer.write_string("abc") == 3u);
        TEST_EXPECT(context, buffer.str() == "abc");
    }

    TEST_CASE(io_writer_wraps_string_buffer_streams) {
        char backing[8] = {};
        StringBuffer buffer;

        buffer.init_with_backing(backing, sizeof(backing));

        io::Writer const writer = io::string_buffer_writer(&buffer);
        TEST_EXPECT(context, io::has_stream_mode(io::query(writer), io::StreamMode::WRITE));

        size_t written = 0u;
        TEST_EXPECT(context, io::write_full(writer, "abc", &written) == io::Error::NONE);
        TEST_EXPECT(context, written == 3u);
        TEST_EXPECT(context, buffer.str() == "abc");

        TEST_EXPECT(context, io::write_fill(writer, 'x', 3u, &written) == io::Error::NONE);
        TEST_EXPECT(context, written == 6u);
        TEST_EXPECT(context, buffer.str() == "abcxxx");

        TEST_EXPECT(context,
                    io::write_full(writer, "overflow", &written) == io::Error::BUFFER_FULL);
        TEST_EXPECT(context, written == 8u);
        TEST_EXPECT(context, buffer.str() == "abcxxxov");
    }

    TEST_CASE(printf_prints_str_ref_values) {
        std::FILE* const file = open_temp_file();
        TEST_EXPECT(context, file != nullptr);

        if (file == nullptr) {
            return;
        }

        char const text[] = {'a', 'l', 'p', 'h', 'a', '\0', 'o', 'm', 'e', 'g', 'a'};
        StrRef const embedded_null_text(text, sizeof(text));

        int const written =
            fmt::fprintf(file, "name=%s size=%zu", embedded_null_text, embedded_null_text.size());

        char const expected[] = {
            'n', 'a', 'm', 'e', '=', 'a', 'l', 'p', 'h', 'a', '\0', 'o',
            'm', 'e', 'g', 'a', ' ', 's', 'i', 'z', 'e', '=', '1',  '1',
        };

        TEST_EXPECT(context, written == static_cast<int>(sizeof(expected)));
        expect_file_text(context, file, StrRef(expected, sizeof(expected)));
        std::fclose(file);
    }

    TEST_CASE(printf_applies_width_and_precision_to_str_ref_values) {
        std::FILE* const file = open_temp_file();
        TEST_EXPECT(context, file != nullptr);

        if (file == nullptr) {
            return;
        }

        int const written =
            fmt::fprintf(file, "[%8s][%.3s][%-5s]", StrRef("abc"), StrRef("abcdef"), StrRef("xy"));
        StrRef const expected = "[     abc][abc][xy   ]";

        TEST_EXPECT(context, written == static_cast<int>(expected.size()));
        expect_file_text(context, file, expected);
        std::fclose(file);
    }

    TEST_CASE(printf_applies_dynamic_width_and_precision_to_str_ref_values) {
        std::FILE* const file = open_temp_file();
        TEST_EXPECT(context, file != nullptr);

        if (file == nullptr) {
            return;
        }

        int const written = fmt::fprintf(file,
                                         "[%*s][%*s][%.*s][%.*s][%*.*s]",
                                         6,
                                         StrRef("abc"),
                                         -5,
                                         StrRef("xy"),
                                         3,
                                         StrRef("abcdef"),
                                         -1,
                                         StrRef("abcdef"),
                                         8,
                                         4,
                                         StrRef("abcdef"));
        StrRef const expected = "[   abc][xy   ][abc][abcdef][    abcd]";

        TEST_EXPECT(context, written == static_cast<int>(expected.size()));
        expect_file_text(context, file, expected);
        std::fclose(file);
    }

    TEST_CASE(printf_applies_dynamic_width_and_precision_to_standard_values) {
        std::FILE* const file = open_temp_file();
        TEST_EXPECT(context, file != nullptr);

        if (file == nullptr) {
            return;
        }

        int const written =
            fmt::fprintf(file, "[%0*d][%*d][%.*x][%*.*u]", 4, 7, -5, 9, 4, 0xabu, 6, 4, 7u);
        StrRef const expected = "[0007][9    ][00ab][  0007]";

        TEST_EXPECT(context, written == static_cast<int>(expected.size()));
        expect_file_text(context, file, expected);
        std::fclose(file);
    }

    TEST_CASE(printf_keeps_literal_text_when_extra_args_are_unused) {
        std::FILE* const file = open_temp_file();
        TEST_EXPECT(context, file != nullptr);

        if (file == nullptr) {
            return;
        }

        int const written = fmt::fprintf(file, "literal %% tail", 123);
        StrRef const expected = "literal % tail";

        TEST_EXPECT(context, written == static_cast<int>(expected.size()));
        expect_file_text(context, file, expected);
        std::fclose(file);
    }

    TEST_CASE(printf_formats_owned_integer_bool_pointer_and_float_values) {
        std::FILE* const file = open_temp_file();
        TEST_EXPECT(context, file != nullptr);

        if (file == nullptr) {
            return;
        }

        int value = 7;
        int const written = fmt::fprintf(
            file, "[%+05d][%#x][%#b][%v][%.2f][%p]", -42, 0x2au, 5u, true, 3.5, &value);
        StrRef const prefix = "[-0042][0x2a][0b101][true][3.50][0x";

        TEST_EXPECT(context, written > static_cast<int>(prefix.size()));

        char buffer[128] = {};
        bool const positioned = std::fseek(file, 0, SEEK_SET) == 0;
        TEST_EXPECT(context, positioned);

        if (positioned) {
            size_t const read_size = std::fread(buffer, 1u, sizeof(buffer), file);
            StrRef const text(buffer, read_size);

            TEST_EXPECT(context, text.starts_with(prefix));
            TEST_EXPECT(context, text.ends_with(']'));
        }

        std::fclose(file);
    }

    TEST_CASE(printf_formats_into_fixed_allocator_and_thread_temp_buffers) {
        char fixed_backing[64] = {};
        StrRef const fixed =
            fmt::bprintf(fixed_backing, sizeof(fixed_backing), "%s:%04u", StrRef("item"), 9u);

        TEST_EXPECT(context, fixed == "item:0009");
        TEST_EXPECT(context, fixed.data() == fixed_backing);

        Arena arena;
        TEST_EXPECT(context, arena.init({64u * 1024u, 64u * 1024u}));

        if (!arena.initialized()) {
            return;
        }

        StringBuffer allocated = fmt::aprintf(arena.resource(), "value=%v", 42);
        uintptr_t const allocated_address = reinterpret_cast<uintptr_t>(allocated.data());
        uintptr_t const arena_begin = reinterpret_cast<uintptr_t>(arena.data());
        uintptr_t const arena_end = arena_begin + arena.reserved_size();

        TEST_EXPECT(context, allocated.str() == "value=42");
        TEST_EXPECT(context, allocated_address >= arena_begin);
        TEST_EXPECT(context, allocated_address < arena_end);

        StrRef const temporary = fmt::tprintf("%s %v", StrRef("temp"), 13);

        TEST_EXPECT(context, temporary == "temp 13");
        TEST_EXPECT(context, !temporary.empty());
    }

} // namespace

TEST_MAIN()
