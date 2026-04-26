#pragma once

#include <cstddef>
#include <cstdint>

namespace test {

    struct Context {
        uint32_t failed_assertions;
    };

    using TestFn = void (*)(Context* context);

    struct TestCase {
        char const* name;
        TestFn fn;
    };

    struct Registration {
        Registration(char const* name, TestFn fn) noexcept;

        TestCase test_case;
        Registration* next;
    };

    auto expect(Context* context,
                bool condition,
                char const* expression,
                char const* file,
                uint32_t line) -> bool;
    auto run_tests(TestCase const* test_cases, size_t test_case_count) -> int;
    auto run_registered_tests() -> int;

} // namespace test

#define TEST_DETAIL_CONCAT_INNER(left, right) left##right
#define TEST_DETAIL_CONCAT(left, right) TEST_DETAIL_CONCAT_INNER(left, right)

#define TEST_CASE(name)                                                                            \
    static auto name(::test::Context* context) -> void;                                            \
    static ::test::Registration TEST_DETAIL_CONCAT(test_registration_, __LINE__)(#name, (name));   \
    static auto name(::test::Context* context) -> void

#define TEST_EXPECT(context, expression)                                                           \
    (::test::expect((context),                                                                     \
                    static_cast<bool>(expression),                                                 \
                    #expression,                                                                   \
                    __FILE__,                                                                      \
                    static_cast<uint32_t>(__LINE__)))

#define TEST_MAIN()                                                                                \
    auto main() -> int {                                                                           \
        return ::test::run_registered_tests();                                                     \
    }
