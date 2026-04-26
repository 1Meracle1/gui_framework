#pragma once

#include <cstddef>

[[nodiscard]] auto virtual_page_size()  -> size_t;

[[nodiscard]] auto virtual_reserve(size_t size)  -> void*;
[[nodiscard]] auto virtual_commit(void* data, size_t size)  -> bool;
[[nodiscard]] auto virtual_decommit(void* data, size_t size)  -> bool;
[[nodiscard]] auto virtual_release(void* data, size_t size)  -> bool;
