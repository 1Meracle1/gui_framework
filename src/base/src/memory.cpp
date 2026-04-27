#include "base/assert.h"

#include <algorithm>
#include <array>
#include <base/config.h>
#include <base/memory.h>
#include <base/virtual_memory.h>
#include <bit>
#include <cstdint>
#include <limits>

namespace {
    [[nodiscard]] auto is_power_of_two(size_t value) -> bool {
        return value != 0u && (value & (value - 1u)) == 0u;
    }

    [[nodiscard]] auto align_forward(size_t value, size_t alignment) -> size_t {
        ASSERT(is_power_of_two(alignment));
        size_t const mask = alignment - 1u;
        ASSERT(value <= std::numeric_limits<size_t>::max() - mask);
        return (value + mask) & ~mask;
    }

    [[nodiscard]] auto align_address_forward(uintptr_t value, size_t alignment) -> uintptr_t {
        ASSERT(is_power_of_two(alignment));
        uintptr_t const mask = static_cast<uintptr_t>(alignment - 1u);
        ASSERT(value <= std::numeric_limits<size_t>::max() - mask);
        return (value + mask) & ~mask;
    }

    [[nodiscard]] auto byte_add(void* data, size_t offset) -> void* {
        return static_cast<void*>(static_cast<std::byte*>(data) + offset);
    }

    struct ThreadTempArenaState {
        auto init(ThreadTempArenaOptions const& options) -> void {
            if (!initialized) {
                ArenaOptions const arena_options = {options.reserve_size, options.commit_size};
                for (auto& arena : arenas) {
                    arena.init(arena_options);
                }
                initialized = true;
            }
        }

        auto shutdown() -> void {
            for (auto& arena : arenas) {
                arena.destroy();
            }
            initialized = false;
        }

        auto reset() -> void {
            for (auto& arena : arenas) {
                arena.reset();
            }
        }

        std::array<Arena, THREAD_TEMP_ARENA_COUNT> arenas;
        bool initialized = false;
    };

    thread_local ThreadTempArenaState thread_temp_state = {};

    [[nodiscard]] auto require_thread_temp_state() -> ThreadTempArenaState& {
        thread_temp_state.init({});
        return thread_temp_state;
    }

} // namespace

Arena::~Arena() {
    destroy();
}

auto Arena::init(ArenaOptions const& options) -> void {
    ASSERT(m_memory == nullptr && options.reserve_size > 0u &&
           options.commit_size <= options.reserve_size);

    size_t const page_size = virtual_page_size();
    m_reserved_size = align_forward(options.reserve_size, page_size);
    m_committed_size = align_forward(std::max(options.commit_size, page_size), page_size);

    m_memory = virtual_reserve(m_reserved_size);
    ASSERT(m_memory != nullptr);
    ASSERT(virtual_commit(m_memory, m_committed_size));

    m_initial_commit_size = m_committed_size;
    m_commit_size = m_committed_size;
    m_used_size = 0u;
}

auto Arena::destroy() -> void {
    if (m_memory != nullptr) {
        BASE_UNUSED(virtual_release(m_memory, m_reserved_size));
    }

    m_memory = nullptr;
    m_reserved_size = 0u;
    m_committed_size = 0u;
    m_initial_commit_size = 0u;
    m_commit_size = 0u;
    m_used_size = 0u;
}

auto Arena::allocate_bytes(size_t size, size_t alignment) -> void* {
    ASSERT(m_memory != nullptr);
    ASSERT(size > 0u);
    ASSERT(is_power_of_two(alignment));

    uintptr_t const base = std::bit_cast<uintptr_t>(m_memory);
    uintptr_t const current = base + m_used_size;
    uintptr_t const aligned = align_address_forward(current, alignment);

    size_t const offset = static_cast<size_t>(aligned - base);
    ASSERT(offset <= m_reserved_size && size <= m_reserved_size - offset);

    size_t const new_used_size = offset + size;
    ASSERT(new_used_size <= m_committed_size);
    commit_to(new_used_size);
    m_used_size = new_used_size;
    return std::bit_cast<void*>(aligned);
}

auto Arena::reset() -> void {
    m_used_size = 0u;
}

auto Arena::reset_to(ArenaMarker marker) -> void {
    ASSERT(marker.used_size <= m_used_size);
    ASSERT(marker.used_size <= m_reserved_size);
    m_used_size = marker.used_size;
}

auto Arena::trim_committed_pages() -> void {
    if (m_memory == nullptr || m_committed_size <= m_initial_commit_size) {
        return;
    }
    size_t const page_size = virtual_page_size();
    size_t const keep_size = std::max(align_forward(m_used_size, page_size), m_initial_commit_size);
    if (keep_size >= m_committed_size) {
        return;
    }

    void* const decommit_begin = byte_add(m_memory, keep_size);
    size_t const decommit_size = m_committed_size - keep_size;
    ASSERT(virtual_decommit(decommit_begin, decommit_size));
    m_committed_size = keep_size;
}

auto Arena::marker() const -> ArenaMarker {
    return ArenaMarker{m_used_size};
}

auto Arena::initialized() const -> bool {
    return m_memory != nullptr;
}

auto Arena::data() -> void* {
    return m_memory;
}

auto Arena::data() const -> void const* {
    return m_memory;
}

auto Arena::used_size() const -> size_t {
    return m_used_size;
}

auto Arena::committed_size() const -> size_t {
    return m_committed_size;
}

auto Arena::reserved_size() const -> size_t {
    return m_reserved_size;
}

auto Arena::remaining_size() const -> size_t {
    return m_reserved_size - m_used_size;
}

auto Arena::resource() -> std::pmr::memory_resource* {
    return this;
}

auto Arena::do_allocate(size_t bytes, size_t alignment) -> void* {
    return allocate_bytes(bytes, alignment);
}

auto Arena::do_deallocate(void* p, size_t bytes, size_t alignment) -> void {
    BASE_UNUSED(p);
    BASE_UNUSED(bytes);
    BASE_UNUSED(alignment);
}

auto Arena::do_is_equal(std::pmr::memory_resource const& other) const noexcept -> bool {
    return this == &other;
}

auto Arena::commit_to(size_t needed_size) -> void {
    ASSERT(needed_size <= m_reserved_size);
    size_t const target_size = std::min(align_forward(needed_size, m_commit_size), m_reserved_size);
    if (target_size <= m_committed_size) {
        return;
    }
    void* const commit_begin = byte_add(m_memory, m_committed_size);
    size_t const commit_size = target_size - m_committed_size;
    ASSERT(virtual_commit(commit_begin, commit_size));
    m_committed_size = target_size;
}

ArenaTemp::ArenaTemp(Arena& arena) : m_arena(&arena), m_marker(arena.marker()) {}

ArenaTemp::~ArenaTemp() {
    end();
}

ArenaTemp::ArenaTemp(ArenaTemp&& other) : m_arena(other.m_arena), m_marker(other.m_marker) {
    other.m_arena = nullptr;
    other.m_marker = {};
}

auto ArenaTemp::operator=(ArenaTemp&& other) -> ArenaTemp& {
    if (this != &other) {
        end();
        m_arena = other.m_arena;
        m_marker = other.m_marker;
        other.m_arena = nullptr;
        other.m_marker = {};
    }

    return *this;
}

auto ArenaTemp::end() -> void {
    if (m_arena != nullptr) {
        m_arena->reset_to(m_marker);
        m_arena = nullptr;
        m_marker = {};
    }
}

auto ArenaTemp::keep() -> void {
    m_arena = nullptr;
    m_marker = {};
}

auto ArenaTemp::arena() -> Arena* {
    return m_arena;
}

auto ArenaTemp::arena() const -> Arena const* {
    return m_arena;
}

auto init_thread_temp_arenas(ThreadTempArenaOptions const& options) -> void {
    thread_temp_state.init(options);
}

auto shutdown_thread_temp_arenas() -> void {
    thread_temp_state.shutdown();
}

auto reset_thread_temp_arenas() -> void {
    thread_temp_state.reset();
}

auto thread_temp_arena(uint32_t index) -> Arena& {
    ThreadTempArenaState& state = require_thread_temp_state();
    ASSERT(index < state.arenas.size());
    return state.arenas[index];
}

auto thread_temp_resource(uint32_t index) -> std::pmr::memory_resource* {
    return thread_temp_arena(index).resource();
}

auto begin_thread_temp_arena(uint32_t index) -> ArenaTemp {
    return ArenaTemp(thread_temp_arena(index));
}
