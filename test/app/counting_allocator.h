#pragma once

#include <chrono>
#include <vector>

class counts_for_allocator {
    struct measurement_internal {
        std::chrono::steady_clock::time_point m_tp{};
        size_t m_diff{};
    };

    struct measurement {
        std::chrono::steady_clock::duration m_duration{};
        size_t m_num_bytes_allocated{};
    };

    std::vector<measurement_internal> m_measurements{};
    std::chrono::steady_clock::time_point m_start = std::chrono::steady_clock::now();

public:
    using measures_type = std::vector<measurement>;

    void add(size_t count) {
        m_measurements.emplace_back(measurement_internal{std::chrono::steady_clock::now(), count});
    }

    void sub(size_t count) {
        // overflow, but it's ok
        m_measurements.emplace_back(measurement_internal{std::chrono::steady_clock::now(), 0U - count});
    }

    [[nodiscard]] auto calc_measurements() const -> measures_type {
        auto measurements = measures_type{};
        auto total_bytes = size_t();
        auto const start_time = m_start;
        for (auto const& m : m_measurements) {
            total_bytes += m.m_diff;
            measurements.emplace_back(measurement{m.m_tp - start_time, total_bytes});
        }
        return measurements;
    }

    [[nodiscard]] auto size() const -> size_t {
        return m_measurements.size();
    }

    void reset() {
        m_measurements.clear();
        m_start = std::chrono::steady_clock::now();
    }
};

/**
 * Forwards all allocations/deallocations to the counts
 */
template <class T>
class counting_allocator {
    counts_for_allocator* m_counts;

    template <typename U>
    friend class counting_allocator;

public:
    using value_type = T;

    /**
     * Not explicit so we can easily construct it with the correct resource
     */
    counting_allocator(counts_for_allocator* counts) noexcept // NOLINT(google-explicit-constructor,hicpp-explicit-conversions)
        : m_counts(counts) {}

    template <class U>
    explicit counting_allocator(counting_allocator<U> const& other) noexcept
        : m_counts(other.m_counts) {}

    counting_allocator(counting_allocator const& other) noexcept = default;
    counting_allocator(counting_allocator&& other) noexcept = default;
    auto operator=(counting_allocator const& other) noexcept -> counting_allocator& = default;
    auto operator=(counting_allocator&& other) noexcept -> counting_allocator& = default;
    ~counting_allocator() = default;

    auto allocate(size_t n) -> T* {
        m_counts->add(sizeof(T) * n);
        return std::allocator<T>{}.allocate(n);
    }

    void deallocate(T* p, size_t n) noexcept {
        m_counts->sub(sizeof(T) * n);
        std::allocator<T>{}.deallocate(p, n);
    }

    template <class U>
    friend auto operator==(counting_allocator const& a, counting_allocator<U> const& b) noexcept -> bool {
        return a.m_counts == b.m_counts;
    }

    template <class U>
    friend auto operator!=(counting_allocator const& a, counting_allocator<U> const& b) noexcept -> bool {
        return a.m_counts != b.m_counts;
    }
};
