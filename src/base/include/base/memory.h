#pragma once

#include <base/assert.h>
#include <cstddef>
#include <cstdint>
#include <memory_resource>
#include <utility>

using MemoryResource = std::pmr::memory_resource;

inline constexpr size_t DEFAULT_ARENA_RESERVE_SIZE = 256u * 1024u * 1024u;
inline constexpr size_t DEFAULT_ARENA_COMMIT_SIZE = 64u * 1024u;
inline constexpr size_t DEFAULT_THREAD_TEMP_ARENA_RESERVE_SIZE = 64u * 1024u * 1024u;
inline constexpr uint32_t THREAD_TEMP_ARENA_COUNT = 2u;

struct ArenaOptions {
    size_t reserve_size = DEFAULT_ARENA_RESERVE_SIZE;
    size_t commit_size = DEFAULT_ARENA_COMMIT_SIZE;
};

struct ArenaMarker {
    size_t used_size;
};

class Arena final : public std::pmr::memory_resource {
  public:
    Arena() = default;
    ~Arena() override;

    Arena(Arena&&) = delete;
    Arena(Arena const&) = delete;
    auto operator=(Arena&&) -> Arena& = delete;
    auto operator=(Arena const&) -> Arena& = delete;

    [[nodiscard]] auto init(ArenaOptions const& options = {}) -> bool;
    auto destroy() -> void;

    [[nodiscard]] auto allocate_bytes(size_t size, size_t alignment = alignof(void*)) -> void*;
    [[nodiscard]] auto try_allocate_bytes(size_t size, size_t alignment = alignof(void*)) -> void*;

    auto reset() -> void;
    auto reset_to(ArenaMarker marker) -> void;
    auto trim_committed_pages() -> bool;

    [[nodiscard]] auto marker() const -> ArenaMarker;
    [[nodiscard]] auto initialized() const -> bool;
    [[nodiscard]] auto data() -> void*;
    [[nodiscard]] auto data() const -> void const*;
    [[nodiscard]] auto used_size() const -> size_t;
    [[nodiscard]] auto committed_size() const -> size_t;
    [[nodiscard]] auto reserved_size() const -> size_t;
    [[nodiscard]] auto remaining_size() const -> size_t;
    [[nodiscard]] auto resource() -> std::pmr::memory_resource*;

  private:
    auto do_allocate(size_t bytes, size_t alignment) -> void* override;
    auto do_deallocate(void* p, size_t bytes, size_t alignment) -> void override;
    [[nodiscard]] auto do_is_equal(std::pmr::memory_resource const& other) const noexcept -> bool override;
    [[nodiscard]] auto commit_to(size_t needed_size) -> bool;

  private:
    void* m_memory = nullptr;
    size_t m_reserved_size = 0u;
    size_t m_committed_size = 0u;
    size_t m_initial_commit_size = 0u;
    size_t m_commit_size = 0u;
    size_t m_used_size = 0u;
};

class ArenaTemp {
  public:
    explicit ArenaTemp(Arena& arena);
    ~ArenaTemp();

    ArenaTemp(ArenaTemp&& other);
    ArenaTemp(ArenaTemp const&) = delete;
    auto operator=(ArenaTemp&& other) -> ArenaTemp&;
    auto operator=(ArenaTemp const&) -> ArenaTemp& = delete;

    auto end() -> void;
    auto keep() -> void;

    [[nodiscard]] auto arena() -> Arena*;
    [[nodiscard]] auto arena() const -> Arena const*;

  private:
    Arena* m_arena = nullptr;
    ArenaMarker m_marker = {};
};

struct ThreadTempArenaOptions {
    size_t reserve_size = DEFAULT_THREAD_TEMP_ARENA_RESERVE_SIZE;
    size_t commit_size = DEFAULT_ARENA_COMMIT_SIZE;
};

// Call from a thread entry point to choose non-default temporary arena sizes.
// First use on a thread initializes with defaults if this was not called.
[[nodiscard]] auto init_thread_temp_arenas(ThreadTempArenaOptions const& options = {}) -> bool;
auto shutdown_thread_temp_arenas() -> void;

// Call once per frame on each thread that uses thread-local temporary arenas.
auto reset_thread_temp_arenas() -> void;

[[nodiscard]] auto thread_temp_arena(uint32_t index = 0u) -> Arena&;
[[nodiscard]] auto thread_temp_resource(uint32_t index = 0u) -> std::pmr::memory_resource*;
[[nodiscard]] auto begin_thread_temp_arena(uint32_t index = 0u) -> ArenaTemp;

template <typename T> [[nodiscard]] auto arena_alloc(Arena& arena, size_t count = 1u) -> T* {
    ASSERT(count > 0);
    ASSERT(count <= (static_cast<size_t>(-1) / sizeof(T)));
    return static_cast<T*>(arena.allocate_bytes(sizeof(T) * count, alignof(T)));
}

template <typename T, typename... Args>
[[nodiscard]] auto arena_new(Arena& arena, Args&&... args) -> T* {
    void* const memory = arena.allocate_bytes(sizeof(T), alignof(T));
    return new (memory) T(std::forward<Args>(args)...);
}
