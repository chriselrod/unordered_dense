#include <ankerl/unordered_dense.h>
#include <app/counter.h>

#include <doctest.h>

TEST_CASE("extract") {
    auto counts = counter();
    INFO(counts);

    using map_t = ankerl::unordered_dense::map<counter::obj, counter::obj>;
    auto container = map_t::value_container_type();
    {
        auto map = map_t();

        for (size_t i = 0; i < 100; ++i) {
            map.try_emplace(counter::obj{i, counts}, i, counts);
        }

        container = std::move(map).extract();
    }

    REQUIRE(container.size() == 100U);
    for (size_t i = 0; i < container.size(); ++i) {
        REQUIRE(container[i].first.get() == i);
        REQUIRE(container[i].second.get() == i);
    }
}
