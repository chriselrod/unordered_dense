#ifndef ANKERL_SEGMENTED_VECTOR_H
#define ANKERL_SEGMENTED_VECTOR_H

#include <cstdint>
#include <memory>
#include <type_traits>
#include <utility> // for exchange, pair, ...
#include <vector>

namespace ankerl {

// Very much like std::deque, but faster for indexing (in most cases). As of now this doesn't implement the full std::vector
// API, but merely what's necessary to work as an underlying container for ankerl::unordered_dense::map/set.
template <typename T, typename Allocator = std::allocator<T>, size_t BlockSizeBytes = 4096>
class segmented_vector {
    // Calculates the maximum number of s*2^x thats <= max_val
    static constexpr auto num_bits_closest(size_t max_val, size_t s) -> size_t {
        auto f = size_t{0};
        while (s << (f + 1) <= max_val) {
            ++f;
        }
        return f;
    }

    using self_t = segmented_vector<T, Allocator>;
    using vec_alloc = typename std::allocator_traits<Allocator>::template rebind_alloc<T*>;
    using vec_alloc_traits = std::allocator_traits<vec_alloc>;
    static constexpr auto num_bits = num_bits_closest(BlockSizeBytes, sizeof(T));
    static constexpr auto num_elements_in_block = 1U << num_bits;
    static constexpr auto mask = num_elements_in_block - 1U;

    std::vector<T*, vec_alloc> m_blocks{};
    size_t m_size{};

    template <bool IsConst>
    class iter_t {
        using ptr_t = typename std::conditional_t<IsConst, T const* const*, T**>;
        ptr_t m_data{};
        size_t m_idx{};

        template <bool B>
        friend class iter_t;

    public:
        using difference_type = std::ptrdiff_t;
        using value_type = typename self_t::value_type;
        using reference = typename std::conditional<IsConst, value_type const&, value_type&>::type;
        using pointer = typename std::conditional<IsConst, value_type const*, value_type*>::type;
        using iterator_category = std::forward_iterator_tag;

        iter_t() noexcept = default;

        template <bool OtherIsConst, typename = typename std::enable_if<IsConst && !OtherIsConst>::type>
        // NOLINTNEXTLINE(google-explicit-constructor,hicpp-explicit-conversions)
        constexpr iter_t(iter_t<OtherIsConst> const& other) noexcept
            : m_data(other.m_data)
            , m_idx(other.m_idx) {}

        constexpr iter_t(ptr_t data, size_t idx) noexcept
            : m_data(data)
            , m_idx(idx) {}

        template <bool OtherIsConst, typename = typename std::enable_if<IsConst && !OtherIsConst>::type>
        constexpr auto operator=(iter_t<OtherIsConst> const& other) noexcept -> iter_t& {
            m_data = other.m_data;
            m_idx = other.m_idx;
            return *this;
        }

        constexpr auto operator++() noexcept -> iter_t& {
            ++m_idx;
            return *this;
        }

        constexpr auto operator+(difference_type diff) noexcept -> iter_t {
            return {m_data, static_cast<size_t>(static_cast<difference_type>(m_idx) + diff)};
        }

        template <bool OtherIsConst>
        constexpr auto operator-(iter_t<OtherIsConst> const& other) noexcept -> difference_type {
            return static_cast<difference_type>(m_idx) - static_cast<difference_type>(other.m_idx);
        }

        constexpr auto operator*() const noexcept -> reference {
            return m_data[m_idx >> num_bits][m_idx & mask];
        }

        constexpr auto operator->() const noexcept -> pointer {
            return &m_data[m_idx >> num_bits][m_idx & mask];
        }

        template <bool O>
        constexpr auto operator==(iter_t<O> const& o) const noexcept -> bool {
            return m_idx == o.m_idx && m_data == o.m_data;
        }

        template <bool O>
        constexpr auto operator!=(iter_t<O> const& o) const noexcept -> bool {
            return !(*this == o);
        }
    };

    // slow path: need to allocate a new segment every once in a while
    void increase_capacity() {
        auto ba = Allocator(m_blocks.get_allocator());
        auto* block = std::allocator_traits<Allocator>::allocate(ba, num_elements_in_block);
        m_blocks.push_back(block);
    }

    void append_everything_from(segmented_vector&& other) {
        reserve(size() + other.size());
        for (auto&& o : other) {
            emplace_back(std::move(o));
        }
    }
    void append_everything_from(segmented_vector const& other) {
        reserve(size() + other.size());
        for (auto const& o : other) {
            emplace_back(o);
        }
    }

    void dealloc() {
        auto ba = Allocator(m_blocks.get_allocator());
        for (auto* ptr : m_blocks) {
            std::allocator_traits<Allocator>::deallocate(ba, ptr, num_elements_in_block);
        }
    }

    [[nodiscard]] static constexpr auto calc_num_blocks_for_capacity(size_t capacity) {
        return (capacity + num_elements_in_block - 1U) / num_elements_in_block;
    }

public:
    using value_type = T;
    using allocator_type = Allocator;
    using iterator = iter_t<false>;
    using const_iterator = iter_t<true>;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;
    using reference = T&;
    using const_reference = T const&;
    using pointer = T*;
    using const_pointer = T const*;

    segmented_vector() = default;

    // NOLINTNEXTLINE(google-explicit-constructor,hicpp-explicit-conversions)
    segmented_vector(Allocator alloc)
        : m_blocks(vec_alloc(alloc)) {}

    segmented_vector(segmented_vector&& other, Allocator alloc)
        : m_blocks(vec_alloc(alloc)) {
        if (other.get_allocator() == alloc) {
            *this = std::move(other);
        } else {
            // Oh my, allocator is different so we need to copy everything.
            append_everything_from(std::move(other));
        }
    }

    segmented_vector(segmented_vector&& other) noexcept
        : m_blocks(std::move(other.m_blocks))
        , m_size(std::exchange(other.m_size, {})) {}

    segmented_vector(segmented_vector const& other, Allocator alloc)
        : m_blocks(vec_alloc(alloc)) {
        append_everything_from(other);
    }

    segmented_vector(segmented_vector const& other) {
        append_everything_from(other);
    }

    auto operator=(segmented_vector const& other) -> segmented_vector& {
        if (this == &other) {
            return *this;
        }
        clear();
        append_everything_from(other);
        return *this;
    }

    auto operator=(segmented_vector&& other) noexcept -> segmented_vector& {
        clear();
        dealloc();
        m_blocks = std::move(other.m_blocks);
        m_size = std::exchange(other.m_size, {});
        return *this;
    }

    ~segmented_vector() {
        clear();
        dealloc();
    }

    [[nodiscard]] constexpr auto size() const -> size_t {
        return m_size;
    }

    [[nodiscard]] constexpr auto capacity() const -> size_t {
        return m_blocks.size() * num_elements_in_block;
    }

    // Indexing is highly performance critical
    [[nodiscard]] constexpr auto operator[](size_t i) const noexcept -> T const& {
        return m_blocks[i >> num_bits][i & mask];
    }

    [[nodiscard]] constexpr auto operator[](size_t i) noexcept -> T& {
        return m_blocks[i >> num_bits][i & mask];
    }

    [[nodiscard]] constexpr auto begin() -> iterator {
        return {m_blocks.data(), 0U};
    }
    [[nodiscard]] constexpr auto begin() const -> const_iterator {
        return {m_blocks.data(), 0U};
    }
    [[nodiscard]] constexpr auto cbegin() const -> const_iterator {
        return {m_blocks.data(), 0U};
    }

    [[nodiscard]] constexpr auto end() -> iterator {
        return {m_blocks.data(), m_size};
    }
    [[nodiscard]] constexpr auto end() const -> const_iterator {
        return {m_blocks.data(), m_size};
    }
    [[nodiscard]] constexpr auto cend() const -> const_iterator {
        return {m_blocks.data(), m_size};
    }

    [[nodiscard]] constexpr auto back() -> reference {
        return operator[](m_size - 1);
    }
    [[nodiscard]] constexpr auto back() const -> const_reference {
        return operator[](m_size - 1);
    }

    void pop_back() {
        back().~T();
        --m_size;
    }

    [[nodiscard]] auto empty() const {
        return 0 == m_size;
    }

    void reserve(size_t new_capacity) {
        m_blocks.reserve(calc_num_blocks_for_capacity(new_capacity));
        while (new_capacity > capacity()) {
            increase_capacity();
        }
    }

    [[nodiscard]] auto get_allocator() const -> allocator_type {
        return allocator_type{m_blocks.get_allocator()};
    }

    template <class... Args>
    auto emplace_back(Args&&... args) -> reference {
        if (m_size == capacity()) {
            increase_capacity();
        }
        auto* ptr = static_cast<void*>(&operator[](m_size));
        auto& ref = *new (ptr) T(std::forward<Args>(args)...);
        ++m_size;
        return ref;
    }

    void clear() {
        if constexpr (!std::is_trivially_destructible_v<T>) {
            for (size_t i = 0, s = size(); i < s; ++i) {
                operator[](i).~T();
            }
        }
        m_size = 0;
    }

    void shrink_to_fit() {
        auto ba = Allocator(m_blocks.get_allocator());
        auto num_blocks_required = calc_num_blocks_for_capacity(m_size);
        while (m_blocks.size() > num_blocks_required) {
            std::allocator_traits<Allocator>::deallocate(ba, m_blocks.back(), num_elements_in_block);
            m_blocks.pop_back();
        }
        m_blocks.shrink_to_fit();
    }
};

} // namespace ankerl

#endif
