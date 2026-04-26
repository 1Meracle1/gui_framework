#!/usr/bin/env sh
set -eu

case "$(uname -s)" in
    Darwin*) default_preset="macos-clang-debug"; exe_suffix="" ;;
    MINGW*|MSYS*|CYGWIN*) default_preset="windows-msvc-debug"; exe_suffix=".exe" ;;
    *) default_preset="macos-clang-debug"; exe_suffix="" ;;
esac

preset="${1:-$default_preset}"

cmake --preset "$preset"
cmake --build --preset "$preset"
"$(dirname "$0")/build/$preset/gui_framework_benchmarks$exe_suffix"
