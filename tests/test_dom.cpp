#include <doctest.h>
#include <rbxl/dom.hpp>

#include <algorithm>
#include <vector>

using namespace rbxl;

TEST_CASE("StringPool interns names stably") {
    StringPool pool;
    NameId a = pool.intern("Size");
    NameId b = pool.intern("Name");
    CHECK(pool.intern("Size") == a);
    CHECK(a != b);
    CHECK(pool.name(a) == "Size");
    CHECK(pool.find("Size") == a);
    CHECK(pool.find("Nope") == kNoName);
    CHECK(pool.size() == 2);
}

TEST_CASE("Dom creates instances and parents them") {
    Dom dom;
    auto root = dom.create("Model");
    auto child = dom.create("Part");
    CHECK(dom.roots().size() == 2);

    dom.setParent(child, root);
    CHECK(dom.at(child).parent == root);
    CHECK(dom.at(root).children.size() == 1);
    CHECK(dom.at(root).children[0] == child);
    CHECK(dom.roots().size() == 1);
    CHECK(dom.roots()[0] == root);
}

TEST_CASE("Reparenting detaches from the previous parent") {
    Dom dom;
    auto a = dom.create("Model");
    auto b = dom.create("Model");
    auto child = dom.create("Part");
    dom.setParent(child, a);
    dom.setParent(child, b);
    CHECK(dom.at(a).children.empty());
    CHECK(dom.at(b).children.size() == 1);
    CHECK(dom.at(child).parent == b);
}

TEST_CASE("Detaching to kNoInstance makes an instance a root again") {
    Dom dom;
    auto parent = dom.create("Model");
    auto child = dom.create("Part");
    dom.setParent(child, parent);
    dom.setParent(child, kNoInstance);
    CHECK(dom.at(parent).children.empty());
    CHECK(dom.roots().size() == 2);
}

TEST_CASE("Properties round-trip through the name pool") {
    Dom dom;
    auto id = dom.create("Part");
    dom.setProperty(id, "Name", std::string("Baseplate"));
    dom.setProperty(id, "Size", Vector3{4, 1, 2});
    CHECK(dom.nameOf(id) == "Baseplate");

    const Variant* size = dom.getProperty(id, "Size");
    REQUIRE(size != nullptr);
    CHECK(variantTypeOf(*size) == VariantType::Vector3);
    CHECK(std::get<Vector3>(*size).x == 4.0f);
    CHECK(dom.getProperty(id, "Missing") == nullptr);
}

TEST_CASE("Setting a property twice overwrites rather than duplicating") {
    Dom dom;
    auto id = dom.create("Part");
    dom.setProperty(id, "Name", std::string("A"));
    dom.setProperty(id, "Name", std::string("B"));
    CHECK(dom.at(id).properties.size() == 1);
    CHECK(dom.nameOf(id) == "B");
}

TEST_CASE("Property storage stays sorted by NameId") {
    Dom dom;
    auto id = dom.create("Part");
    dom.setProperty(id, "Zebra", std::string("z"));
    dom.setProperty(id, "Alpha", std::string("a"));
    dom.setProperty(id, "Middle", std::string("m"));
    const auto& props = dom.at(id).properties;
    for (std::size_t i = 1; i < props.size(); ++i) {
        CHECK(props[i - 1].first < props[i].first);
    }
}

TEST_CASE("postOrder visits descendants before ancestors") {
    // Build the tree from the format spec:
    //   1 -> {2, 3 -> {5, 6}, 4 -> {7}}
    Dom dom;
    auto n1 = dom.create("Folder");
    auto n2 = dom.create("Folder");
    auto n3 = dom.create("Folder");
    auto n4 = dom.create("Folder");
    auto n5 = dom.create("Folder");
    auto n6 = dom.create("Folder");
    auto n7 = dom.create("Folder");
    dom.setParent(n2, n1); dom.setParent(n3, n1); dom.setParent(n4, n1);
    dom.setParent(n5, n3); dom.setParent(n6, n3);
    dom.setParent(n7, n4);

    // Spec states Roblox Studio writes: 2, 5, 6, 3, 7, 4, 1
    std::vector<InstanceId> expected{n2, n5, n6, n3, n7, n4, n1};
    CHECK(dom.postOrder() == expected);
}

// Dom::setParent removes a root via swap-with-last-element + pop (see
// removeRoot in dom.cpp) rather than the vector-shifting erase an earlier
// version used, so that removal is O(1) instead of O(roots_.size()). That
// means roots() no longer preserves relative order across a removal, only
// membership -- checked here as a set, not by position. It also means every
// removal after the first can hit the swap branch, where the element that
// used to be last takes the removed element's slot and its rootIndex_ entry
// must be corrected to match; a stale entry there would misdirect (or,
// out of bounds, corrupt) a later removal of that same swapped-in element.
TEST_CASE("Removing a root swaps with the last element and keeps rootIndex_ consistent") {
    Dom dom;
    auto r0 = dom.create("Model");
    auto r1 = dom.create("Model");
    auto r2 = dom.create("Model");
    auto r3 = dom.create("Model");
    auto host = dom.create("Model");
    REQUIRE(dom.roots().size() == 5);

    auto rootSet = [&dom]() {
        std::vector<InstanceId> ids(dom.roots().begin(), dom.roots().end());
        std::sort(ids.begin(), ids.end());
        return ids;
    };
    auto sorted = [](std::vector<InstanceId> ids) {
        std::sort(ids.begin(), ids.end());
        return ids;
    };

    // Remove a middle root (r1) by parenting it under host, the current last
    // element of roots_. This is exactly the swap branch: r1's slot is not
    // the last slot, so removeRoot must move host into r1's old slot and
    // repoint host's rootIndex_ entry there.
    dom.setParent(r1, host);
    CHECK(rootSet() == sorted({r0, r2, r3, host}));

    // Remove another root (r0), forcing a second swap.
    dom.setParent(r0, host);
    CHECK(rootSet() == sorted({r2, r3, host}));

    // host was the element swapped into a new slot above. Reparenting it now
    // exercises removeRoot on an id whose rootIndex_ entry was rewritten by
    // an earlier swap; a stale entry here would remove the wrong element (or
    // index out of bounds) instead of host.
    auto other = dom.create("Model");
    dom.setParent(host, other);
    CHECK(rootSet() == sorted({r2, r3, other}));
    CHECK(dom.at(host).parent == other);

    // Membership and content of the moved instances are unaffected by which
    // slot they ended up in.
    CHECK(dom.at(r1).parent == host);
    CHECK(dom.at(r0).parent == host);
    CHECK(dom.at(host).children.size() == 2);
}
