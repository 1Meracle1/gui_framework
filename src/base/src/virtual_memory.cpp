#include <base/assert.h>
#include <base/config.h>
#include <base/virtual_memory.h>

#if BASE_PLATFORM_WINDOWS
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
// clang-format off
#include <Windows.h>
// clang-format on
#elif BASE_PLATFORM_MACOS || BASE_PLATFORM_LINUX
#include <sys/mman.h>
#include <unistd.h>
#endif

#include <cstddef>

auto virtual_page_size() -> size_t {
#if BASE_PLATFORM_WINDOWS
    SYSTEM_INFO system_info = {};
    GetSystemInfo(&system_info);
    return static_cast<size_t>(system_info.dwPageSize);
#elif BASE_PLATFORM_MACOS || BASE_PLATFORM_LINUX
    long const page_size = sysconf(_SC_PAGESIZE);
    return page_size > 0 ? static_cast<size_t>(page_size) : 4096u;
#else
    return 4096u;
#endif
}

auto virtual_reserve(size_t size) -> void* {
    ASSERT(size != 0u);

#if BASE_PLATFORM_WINDOWS
    return VirtualAlloc(nullptr, size, MEM_RESERVE, PAGE_NOACCESS);
#elif BASE_PLATFORM_MACOS || BASE_PLATFORM_LINUX
#if defined(MAP_ANONYMOUS)
    constexpr int ANONYMOUS_FLAG = MAP_ANONYMOUS;
#else
    constexpr int ANONYMOUS_FLAG = MAP_ANON;
#endif
    void* const result = mmap(nullptr, size, PROT_NONE, MAP_PRIVATE | ANONYMOUS_FLAG, -1, 0);
    return result == MAP_FAILED ? nullptr : result;
#else
    BASE_UNUSED(size);
    return nullptr;
#endif
}

auto virtual_commit(void* data, size_t size) -> bool {
    ASSERT(data != nullptr && size != 0u);

#if BASE_PLATFORM_WINDOWS
    return VirtualAlloc(data, size, MEM_COMMIT, PAGE_READWRITE) != nullptr;
#elif BASE_PLATFORM_MACOS || BASE_PLATFORM_LINUX
    return mprotect(data, size, PROT_READ | PROT_WRITE) == 0;
#else
    BASE_UNUSED(data);
    BASE_UNUSED(size);
    return false;
#endif
}

auto virtual_decommit(void* data, size_t size) -> bool {
    ASSERT(data != nullptr && size != 0u);

#if BASE_PLATFORM_WINDOWS
    return VirtualFree(data, size, MEM_DECOMMIT) != FALSE;
#elif BASE_PLATFORM_MACOS || BASE_PLATFORM_LINUX
#if BASE_PLATFORM_MACOS && defined(MADV_FREE)
    constexpr int ADVICE = MADV_FREE;
#elif defined(MADV_DONTNEED)
    constexpr int ADVICE = MADV_DONTNEED;
#else
    constexpr int ADVICE = 0;
#endif
    bool const advised = ADVICE == 0 || madvise(data, size, ADVICE) == 0;
    bool const protected_again = mprotect(data, size, PROT_NONE) == 0;
    return advised && protected_again;
#else
    BASE_UNUSED(data);
    BASE_UNUSED(size);
    return false;
#endif
}

auto virtual_release(void* data, size_t size) -> bool {
    ASSERT(data != nullptr && size != 0u);

#if BASE_PLATFORM_WINDOWS
    BASE_UNUSED(size);
    return VirtualFree(data, 0u, MEM_RELEASE) != FALSE;
#elif BASE_PLATFORM_MACOS || BASE_PLATFORM_LINUX
    if (size == 0u) {
        return false;
    }
    return munmap(data, size) == 0;
#else
    BASE_UNUSED(data);
    BASE_UNUSED(size);
    return false;
#endif
}
