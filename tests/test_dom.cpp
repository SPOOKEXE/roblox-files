#include <doctest.h>
#include <rbxl/dom.hpp>

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
