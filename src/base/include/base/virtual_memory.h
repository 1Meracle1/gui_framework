#pragma once

#include <cstddef>

[[nodiscard]] auto virtual_page_size() noexcept -> size_t;

[[nodiscard]] auto virtual_reserve(size_t size) noexcept -> void*;
[[nodiscard]] auto virtual_commit(void* data, size_t size) noexcept -> bool;
[[nodiscard]] auto virtual_decommit(void* data, size_t size) noexcept -> bool;
[[nodiscard]] auto virtual_release(void* data, size_t size) noexcept -> bool;
