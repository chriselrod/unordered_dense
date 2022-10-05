#include <ankerl/segmented_vector.h>
#include <ankerl/unordered_dense.h> // for map, operator==

#include <app/counting_allocator.h>

#include <third-party/nanobench.h>

#include <doctest.h>
#include <fmt/core.h>

#include <deque>
#include <unordered_map>

template <typename Map>
void evaluate_map(Map& map) {
    auto rng = ankerl::nanobench::Rng{1234};
    for (uint64_t i = 0; i < 200'000; ++i) {
        map[rng()] = i;
    }
}

using hash_t = ankerl::unordered_dense::hash<uint64_t>;
using eq_t = std::equal_to<uint64_t>;
using pair_t = std::pair<uint64_t, uint64_t>;
using alloc_t = counting_allocator<pair_t>;

void print_measures(counts_for_allocator::measures_type const& measures) {
    for (auto [dur, bytes] : measures) {
        fmt::print("{}; {}\n", std::chrono::duration<double>(dur).count(), bytes);
    }
}

TEST_CASE("show_allocations_standard" * doctest::skip()) {
    auto counters = counts_for_allocator{};
    {
        using map_t = ankerl::unordered_dense::map<uint64_t, uint64_t, hash_t, eq_t, alloc_t>;
        auto map = map_t(0, hash_t{}, eq_t{}, alloc_t{&counters});
        evaluate_map(map);
    }
    print_measures(counters.calc_measurements());
}

TEST_CASE("show_allocations_std" * doctest::skip()) {
    auto counters = counts_for_allocator{};
    {
        using alloc_std_pair_t = counting_allocator<std::pair<const uint64_t, uint64_t>>;
        using map_t = std::unordered_map<uint64_t, uint64_t, hash_t, eq_t, alloc_std_pair_t>;
        auto map = map_t(0, hash_t{}, eq_t{}, alloc_std_pair_t{&counters});
        evaluate_map(map);
    }
    print_measures(counters.calc_measurements());
}

TEST_CASE("show_allocations_deque" * doctest::skip()) {
    auto counters = counts_for_allocator{};
    {
        using vec_t = std::deque<pair_t, alloc_t>;
        using map_t = ankerl::unordered_dense::map<uint64_t, uint64_t, hash_t, eq_t, vec_t>;
        auto map = map_t(0, hash_t{}, eq_t{}, alloc_t{&counters});
        evaluate_map(map);
    }
    print_measures(counters.calc_measurements());
}

TEST_CASE("show_allocations_segmented_vector" * doctest::skip()) {
    auto counters = counts_for_allocator{};
    {
        using vec_t = ankerl::segmented_vector<pair_t, alloc_t, 65536>;
        using map_t = ankerl::unordered_dense::map<uint64_t, uint64_t, hash_t, eq_t, vec_t>;
        auto map = map_t{0, hash_t{}, eq_t{}, alloc_t{&counters}};
        evaluate_map(map);
    }
    print_measures(counters.calc_measurements());
}
