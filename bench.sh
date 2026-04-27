#!/usr/bin/env sh
set -eu

case "$(uname -s)" in
    Darwin*) default_preset="macos-clang-debug"; exe_suffix="" ;;
    MINGW*|MSYS*|CYGWIN*) default_preset="windows-msvc-debug"; exe_suffix=".exe" ;;
    *) default_preset="macos-clang-debug"; exe_suffix="" ;;
esac

preset="${1:-$default_preset}"
config=Debug
case "$preset" in
    *release*) config=Release ;;
esac

cmake --preset "$preset"
cmake --build --preset "$preset" --parallel
exe="$(dirname "$0")/build/$preset/gui_framework_benchmarks$exe_suffix"
if [ ! -x "$exe" ]; then
    exe="$(dirname "$0")/build/$preset/$config/gui_framework_benchmarks$exe_suffix"
fi
"$exe"
