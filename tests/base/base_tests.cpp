#include <base/assert.h>
#include <base/crash.h>
#include <base/memory.h>
#include <base/str_ref.h>
#include <cstddef>
#include <cstdint>
#include <memory_resource>
#include <string>
#include <string_view>
#include <test/test.h>
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

    TEST_CASE(assert_handler_intercepts_ASSERTions) {
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

} // namespace

TEST_MAIN()
