#include <gui/gui.h>
#include <test/test.h>

namespace {

    void version_is_available(test::Context* context) {
        gui::Version const gui_version = gui::version();

        TEST_EXPECT(context, gui_version.major == 0u);
        TEST_EXPECT(context, gui_version.minor == 1u);
        TEST_EXPECT(context, gui_version.patch == 0u);
        TEST_EXPECT(context, gui::version_string()[0] != '\0');
        TEST_EXPECT(context, gui::build_compiler()[0] != '\0');
    }

    constexpr test::TestCase TESTS[] = {
        {"version_is_available", version_is_available},
    };

} // namespace

int main() {
    constexpr auto TEST_COUNT = sizeof(TESTS) / sizeof(TESTS[0]);
    return test::run_tests(TESTS, TEST_COUNT);
}
