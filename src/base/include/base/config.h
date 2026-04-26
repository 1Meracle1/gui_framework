#pragma once

#if defined(__clang__) && defined(_MSC_VER)
#define BASE_COMPILER_CLANG_CL 1
#define BASE_COMPILER_CLANG 1
#define BASE_COMPILER_MSVC 0
#elif defined(__clang__)
#define BASE_COMPILER_CLANG_CL 0
#define BASE_COMPILER_CLANG 1
#define BASE_COMPILER_MSVC 0
#elif defined(_MSC_VER)
#define BASE_COMPILER_CLANG_CL 0
#define BASE_COMPILER_CLANG 0
#define BASE_COMPILER_MSVC 1
#else
#define BASE_COMPILER_CLANG_CL 0
#define BASE_COMPILER_CLANG 0
#define BASE_COMPILER_MSVC 0
#endif

#if defined(_WIN32)
#define BASE_PLATFORM_WINDOWS 1
#define BASE_PLATFORM_MACOS 0
#define BASE_PLATFORM_UNKNOWN 0
#elif defined(__APPLE__) && defined(__MACH__)
#define BASE_PLATFORM_WINDOWS 0
#define BASE_PLATFORM_MACOS 1
#define BASE_PLATFORM_LINUX 0
#define BASE_PLATFORM_UNKNOWN 0
#elif defined(__linux__)
#define BASE_PLATFORM_WINDOWS 0
#define BASE_PLATFORM_MACOS 0
#define BASE_PLATFORM_LINUX 1
#define BASE_PLATFORM_UNKNOWN 0
#else
#define BASE_PLATFORM_WINDOWS 0
#define BASE_PLATFORM_MACOS 0
#define BASE_PLATFORM_LINUX 0
#define BASE_PLATFORM_UNKNOWN 1
#endif

#if defined(_MSVC_LANG)
#define BASE_CPP_STANDARD _MSVC_LANG
#else
#define BASE_CPP_STANDARD __cplusplus
#endif

#ifndef BASE_DEBUG
#if defined(NDEBUG)
#define BASE_DEBUG 0
#else
#define BASE_DEBUG 1
#endif
#endif

#define BASE_UNUSED(value) static_cast<void>(value)
