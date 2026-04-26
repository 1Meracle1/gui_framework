#include <base/crash.h>
#include <cstdio>
#include <gui/gui.h>

auto check_crash() -> void {
    int const* ptr = nullptr;
    std::printf("result: %d\n", *ptr);
}

auto main() -> int {
    base::install_crash_handlers();

    gui::Version const gui_version = gui::version();

    std::printf("gui %s\n", gui::version_string());
    std::printf("version: %u.%u.%u\n", gui_version.major, gui_version.minor, gui_version.patch);
    std::printf("compiler: %s\n", gui::build_compiler());
    check_crash();
    return 0;
}
