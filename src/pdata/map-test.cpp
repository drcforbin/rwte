#include "pdata/map.h"
#include "pdata/test-helpers.h"
#include "rwte/catch.hpp"

// todo: rename node_count to something different,
// and note that it's intended to ease debugging

// todo: add tests for transient get
// todo: add tests for failed match without
// todo: add tests for without that hits each node type
// todo: test that there's a single transient in a thread,
//       but changes are separate from thread to thread

TEST_CASE("overflows to array node", "[bitmap_indexed_node]")
{
    using base_node = pdata::detail::node<MockHashable, MockHashable, MockHashableHash>;
    using bin_node = pdata::detail::bitmap_indexed_node<MockHashable, MockHashable, MockHashableHash>;
    using arr_node = pdata::detail::array_node<MockHashable, MockHashable, MockHashableHash>;

    std::shared_ptr<base_node> node = std::make_shared<bin_node>(pdata::detail::edit_type{});
    // fill 16 items in (0-16)
    for (int i = 0; i < 16; i++) {
        auto a = MockHashable{uint32_t(i), i};

        // using same val for key and val, it's easier
        base_node::value_type entry{a, a};

        // using same val for key and val, it's easier
        bool addedLeaf = false;
        node = node->assoc(0, MockHashableHash{}(entry.first), entry, addedLeaf);
        REQUIRE(addedLeaf);

        // make sure it's still a bitmap_indexed_node
        auto n = std::dynamic_pointer_cast<bin_node>(node);
        REQUIRE(n);
        REQUIRE(n->node_count() == i + 1);
    }

    // add a 17th item. This should kick it over, causing bin to
    // become an array_node containing 17 bitmap_indexed_node
    {
        int i = 16;
        auto a = MockHashable{uint32_t(i), i};

        // using same val for key and val, it's easier
        base_node::value_type entry{a, a};

        bool addedLeaf = false;
        node = node->assoc(0, MockHashableHash{}(entry.first), entry, addedLeaf);
        REQUIRE(addedLeaf);

        // make sure it's now an array_node
        auto n = std::dynamic_pointer_cast<arr_node>(node);
        REQUIRE(n);
        REQUIRE(n->node_count() == i + 1);

        /*
        todo: want to check whether the array_node's array contains bmis?

        for i = 0; i < 17; i++ {
            if _, ok := n.array[i].(*bitmapIndexedNode); !ok {
                t.Errorf("subnode %d was wrong type %T", i, n.array[i])
            }
        }

        todo: want to check whether the rest of array_node's array contains nothing?
        for ; i < 32; i++ {
            if n.array[i] != nil {
                t.Errorf("subnode %d was wrong type %T", i, n.array[i])
            }
        }
        */
    }
}

TEST_CASE("collide", "[bitmap_indexed_node]")
{
    using base_node = pdata::detail::node<MockHashable, MockHashable, MockHashableHash>;
    using bin_node = pdata::detail::bitmap_indexed_node<MockHashable, MockHashable, MockHashableHash>;

    std::shared_ptr<base_node> node = std::make_shared<bin_node>(pdata::detail::edit_type{});

    // add two different items with same hash value
    auto a = MockHashable{uint32_t(22892882), 3847823};
    base_node::value_type entrya{a, a};
    auto b = MockHashable{uint32_t(22892882), 905657367};
    base_node::value_type entryb{b, b};

    bool addedLeaf = false;
    node = node->assoc(0, MockHashableHash{}(entrya.first), entrya, addedLeaf);
    REQUIRE(addedLeaf);
    addedLeaf = false;
    node = node->assoc(0, MockHashableHash{}(entryb.first), entryb, addedLeaf);
    REQUIRE(addedLeaf);

    // make sure it's still a bitmap_indexed_node
    auto n = std::dynamic_pointer_cast<bin_node>(node);
    REQUIRE(n);

    // we expect the bitmap_indexed_node to contain only a single child
    REQUIRE(n->node_count() == 1);

    /*
    // and that child node should be a hcn
    if n.array[0].key != nil {
        t.Errorf("expected key for child 0 to be nil (memory leak?)")
    }
    if _, ok := n.array[0].val.(*hashCollisionNode); !ok {
        t.Errorf("items did not contain a hashCollisionNode")
    }

    // make sure the rest of the array is nil
    for i := 1; i < len(n.array); i++ {
        if n.array[i].key != nil {
            t.Errorf("expected key for child %d to be nil", i)
        }
        if n.array[i].val != nil {
            t.Errorf("expected val for child %d to be nil", i)
        }
    }
    */
}

TEST_CASE("persistent getset", "[persistent hash map]")
{
    std::vector counts{5, 100, 1000, 10000, 500000};

    SECTION("single getset")
    {
        for (auto count : counts) {
            auto pairs = randomPairs<uint64_t>(count);
            auto m = std::make_shared<pdata::persistent_map<uint64_t, uint64_t>>();
            m = fill(m, pairs);
            REQUIRE(check(m, pairs));
        }
    }

    SECTION("dup getset")
    {
        for (auto count : counts) {
            auto pairs = randomPairs<uint64_t>(count);
            auto dupPairs = randomDupPairs(pairs);
            auto m = std::make_shared<pdata::persistent_map<uint64_t, uint64_t>>();
            m = fill(m, pairs);
            m = fill(m, dupPairs);
            REQUIRE(check(m, dupPairs));
        }
    }
}

TEST_CASE("transient getset", "[transient hash map]")
{
    std::vector counts{5, 100, 1000, 10000, 5000000};

    SECTION("single getset")
    {
        for (auto count : counts) {
            auto pairs = randomPairs<uint64_t>(count);
            auto m = std::make_shared<pdata::persistent_map<uint64_t, uint64_t>>();
            m = fillTransient(m, pairs);
            REQUIRE(check(m, pairs));
        }
    }

    SECTION("dup getset")
    {
        for (auto count : counts) {
            auto pairs = randomPairs<uint64_t>(count);
            auto dupPairs = randomDupPairs(pairs);
            auto m = std::make_shared<pdata::persistent_map<uint64_t, uint64_t>>();
            m = fillTransient(m, pairs);
            m = fillTransient(m, dupPairs);
            REQUIRE(check(m, dupPairs));
        }
    }
}

TEST_CASE("persistent immutability", "[persistent hash map]")
{
    std::vector<std::pair<uint64_t, uint64_t>> pairs{
            {8, 99},
            {10, 108983},
            {13, 600},
            {3545, 1}};

    // start with empty map
    auto last = std::make_shared<pdata::persistent_map<uint64_t, uint64_t>>();
    std::vector<decltype(last)> maps{last};

    for (std::size_t i = 0; i < pairs.size(); i++) {
        const auto& outerPair = pairs[i];

        // assoc one of the elements
        auto m = last->assoc(outerPair.first, outerPair.second);
        maps.push_back(m);

        // for each map so far, check/recheck its length
        for (std::size_t j = 0; j < maps.size(); j++) {
            const auto& m = maps[j];
            REQUIRE(m->size() == j);

            // ...and make sure it returns the right vals or Nil
            for (std::size_t k = 0; k < pairs.size(); k++) {
                const auto& pair = pairs[k];
                if (k < j) {
                    REQUIRE(m->find(pair.first) == pair.second);
                } else {
                    REQUIRE(m->find(pair.first) == std::nullopt);
                }
            }
        }

        last = m;
    }
}

TEST_CASE("persistent dup immutability", "[persistent hash map]")
{
    auto k1 = 8;
    auto k2 = 10;

    // start with empty map, add a couple
    auto m = std::make_shared<pdata::persistent_map<int, int>>()
                     ->assoc(k1, 99)
                     ->assoc(k2, 108983);

    REQUIRE(m->size() == 2);
    REQUIRE(m->find(k1) == 99);
    REQUIRE(m->find(k2) == 108983);

    // replace them
    auto m2 = m->assoc(k1, 9387)->assoc(k2, 3);

    REQUIRE(m->size() == 2);
    REQUIRE(m2->size() == 2);

    REQUIRE(m->find(k1) == 99);
    REQUIRE(m->find(k2) == 108983);
    REQUIRE(m2->find(k1) == 9387);
    REQUIRE(m2->find(k2) == 3);
}

TEST_CASE("persistent without", "[persistent hash map]")
{
    auto k1 = 8;
    auto k2 = 10;

    // start with empty map, add a couple
    auto m = std::make_shared<pdata::persistent_map<int, int>>()
                     ->assoc(k1, 99)
                     ->assoc(k2, 108983)
                     ->assoc(k1, 9387)
                     ->assoc(k2, 3);

    // remove k1
    auto m2 = m->without(k1);

    REQUIRE(m->size() == 2);
    REQUIRE(m2->size() == 1);

    REQUIRE(m->find(k1) == 9387);
    REQUIRE(m->find(k2) == 3);
    REQUIRE(!m2->find(k1));
    REQUIRE(m2->find(k2) == 3);
}

TEST_CASE("transient without", "[transient hash map]")
{
    auto k1 = 8;
    auto k2 = 10;

    // start with empty map, add a couple
    auto m = std::make_shared<pdata::transient_map<int, int>>()
                     ->assoc(k1, 99)
                     ->assoc(k2, 108983)
                     ->assoc(k1, 9387)
                     ->assoc(k2, 3);

    // remove k1 (should affect both)
    auto m2 = m->without(k1);

    REQUIRE(m->size() == 1);
    REQUIRE(m2->size() == 1);

    REQUIRE(!m->find(k1));
    REQUIRE(m->find(k2) == 3);
    REQUIRE(!m2->find(k1));
    REQUIRE(m2->find(k2) == 3);
}

TEST_CASE("persistent bench", "[persistent hash map]")
{
    BENCHMARK_ADVANCED("persistent set 1000")
    (Catch::Benchmark::Chronometer meter)
    {
        auto pairs = randomPairs<uint64_t>(1000);

        meter.measure([&pairs] {
            auto m = std::make_shared<pdata::persistent_map<uint64_t, uint64_t>>();
            m = fill(m, pairs);
        });
    };

    BENCHMARK_ADVANCED("persistent set dup 1000")
    (Catch::Benchmark::Chronometer meter)
    {
        auto pairs = randomPairs<uint64_t>(1000);
        auto dupPairs = randomDupPairs(pairs);

        meter.measure([&pairs, &dupPairs] {
            auto m = std::make_shared<pdata::persistent_map<uint64_t, uint64_t>>();
            m = fill(m, pairs);
            m = fill(m, dupPairs);
        });
    };
}

TEST_CASE("transient bench", "[transient hash map]")
{
    BENCHMARK_ADVANCED("transient set 1000")
    (Catch::Benchmark::Chronometer meter)
    {
        auto pairs = randomPairs<uint64_t>(1000);

        meter.measure([&pairs] {
            auto m = std::make_shared<pdata::persistent_map<uint64_t, uint64_t>>();
            m = fillTransient(m, pairs);
        });
    };

    BENCHMARK_ADVANCED("transient set dup 1000")
    (Catch::Benchmark::Chronometer meter)
    {
        auto pairs = randomPairs<uint64_t>(1000);
        auto dupPairs = randomDupPairs(pairs);

        meter.measure([&pairs, &dupPairs] {
            auto m = std::make_shared<pdata::persistent_map<uint64_t, uint64_t>>();
            m = fillTransient(m, pairs);
            m = fillTransient(m, dupPairs);
        });
    };

    BENCHMARK_ADVANCED("persistent get 1000")
    (Catch::Benchmark::Chronometer meter)
    {
        auto pairs = randomPairs<uint64_t>(1000);
        auto m = std::make_shared<pdata::persistent_map<uint64_t, uint64_t>>();
        m = fillTransient(m, pairs);

        meter.measure([&m, &pairs] {
            check(m, pairs);
        });
    };
}

/*
func TestTransientHashMapContext(t *testing.T) {
	k0 := &atom.Num{Val: big.NewInt(222)}
	k1 := &atom.Num{Val: big.NewInt(8)}
	k2 := &atom.Num{Val: big.NewInt(10)}

	// start with empty map, add a single value (later maps
	// should share it between them)
	m0 := NewPersistentHashMap()
	m0 = m0.Assoc(k0, &atom.Num{Val: big.NewInt(1129374)})

	// make transients with different contexts
	tm1 := m0.AsTransient(1)
	tm2 := m0.AsTransient(2)

	// set differing values for each, making sure that
	// neither depends on the other
	tm1 = tm1.Assoc(k1, &atom.Num{Val: big.NewInt(99)})
	tm2 = tm2.Assoc(k1, &atom.Num{Val: big.NewInt(9387)})
	tm2 = tm2.Assoc(k2, &atom.Num{Val: big.NewInt(3)})
	tm1 = tm1.Assoc(k2, &atom.Num{Val: big.NewInt(108983)})

	// make persistent again
	m1 := tm1.AsPersistent()
	m2 := tm2.AsPersistent()

	if m0.Length() != 1 {
		t.Errorf("m0 was modified by transient operations!")
	}
	if m1.Length() != 3 || m2.Length() != 3 {
		t.Errorf("unexpected length for m1 (%d) or m2 (%d)", m1.Length(), m2.Length())
	}

	// check value and pointer equality; they should both share k0 val
	if !m0.Get(k0).Equals(&atom.Num{Val: big.NewInt(1129374)}) ||
		!m1.Get(k0).Equals(&atom.Num{Val: big.NewInt(1129374)}) ||
		!m2.Get(k0).Equals(&atom.Num{Val: big.NewInt(1129374)}) ||
		m0.Get(k0) != m1.Get(k0) || m1.Get(k0) != m2.Get(k0) {
		t.Error("expected k0 value to be shared")
	}

	if !m1.Get(k1).Equals(&atom.Num{Val: big.NewInt(99)}) {
		t.Errorf("unexpected value for m1 k1, %s", m1.Get(k1))
	}
	if !m1.Get(k2).Equals(&atom.Num{Val: big.NewInt(108983)}) {
		t.Errorf("unexpected value for m1 k2, %s", m1.Get(k2))
	}

	if !m2.Get(k1).Equals(&atom.Num{Val: big.NewInt(9387)}) {
		t.Errorf("unexpected value for m2 k1, %s", m2.Get(k1))
	}
	if !m2.Get(k2).Equals(&atom.Num{Val: big.NewInt(3)}) {
		t.Errorf("unexpected value for m2 k2, %s", m2.Get(k2))
	}
}
*/
