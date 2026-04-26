#include <cstdio>
#include <gui/gui.h>

int main() {
    gui::Version const gui_version = gui::version();

    std::printf("gui %s\n", gui::version_string());
    std::printf("version: %u.%u.%u\n", gui_version.major, gui_version.minor, gui_version.patch);
    std::printf("compiler: %s\n", gui::build_compiler());

    return 0;
}
