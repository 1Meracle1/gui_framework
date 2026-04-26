#!/usr/bin/env sh
set -eu

case "$(uname -s)" in
    Darwin*) default_preset="macos-clang-debug" ;;
    MINGW*|MSYS*|CYGWIN*) default_preset="windows-msvc-debug" ;;
    *) default_preset="macos-clang-debug" ;;
esac

preset="${1:-$default_preset}"

cmake --preset "$preset"
cmake --build --preset "$preset"
