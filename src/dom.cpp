#include <rbxl/dom.hpp>

#include <algorithm>

namespace rbxl {

NameId StringPool::intern(const std::string& name) {
    auto it = index_.find(name);
    if (it != index_.end()) {
        return it->second;
    }
    NameId id = static_cast<NameId>(names_.size());
    names_.push_back(name);
    index_.emplace(name, id);
    return id;
}

NameId StringPool::find(const std::string& name) const {
    auto it = index_.find(name);
    return it == index_.end() ? kNoName : it->second;
}

const std::string& StringPool::name(NameId id) const {
    return names_[id];
}

InstanceId Dom::create(std::string className) {
    InstanceId id = static_cast<InstanceId>(instances_.size());
    Instance& inst = instances_.emplace_back();
    inst.className = std::move(className);
    roots_.push_back(id);
    return id;
}

Instance& Dom::at(InstanceId id) {
    return instances_[id];
}

const Instance& Dom::at(InstanceId id) const {
    return instances_[id];
}

void Dom::setParent(InstanceId child, InstanceId parent) {
    InstanceId oldParent = instances_[child].parent;
    if (oldParent == kNoInstance) {
        auto it = std::find(roots_.begin(), roots_.end(), child);
        if (it != roots_.end()) {
            roots_.erase(it);
        }
    } else {
        auto& siblings = instances_[oldParent].children;
        auto it = std::find(siblings.begin(), siblings.end(), child);
        if (it != siblings.end()) {
            siblings.erase(it);
        }
    }

    instances_[child].parent = parent;
    if (parent == kNoInstance) {
        roots_.push_back(child);
    } else {
        instances_[parent].children.push_back(child);
    }
}

void Dom::setProperty(InstanceId id, const std::string& name, Variant value) {
    NameId nameId = names_.intern(name);
    auto& props = instances_[id].properties;
    auto it = std::lower_bound(props.begin(), props.end(), nameId,
        [](const std::pair<NameId, Variant>& entry, NameId key) {
            return entry.first < key;
        });
    if (it != props.end() && it->first == nameId) {
        it->second = std::move(value);
    } else {
        props.emplace(it, nameId, std::move(value));
    }
}

const Variant* Dom::getProperty(InstanceId id, const std::string& name) const {
    NameId nameId = names_.find(name);
    if (nameId == kNoName) {
        return nullptr;
    }
    const auto& props = instances_[id].properties;
    auto it = std::lower_bound(props.begin(), props.end(), nameId,
        [](const std::pair<NameId, Variant>& entry, NameId key) {
            return entry.first < key;
        });
    if (it != props.end() && it->first == nameId) {
        return &it->second;
    }
    return nullptr;
}

std::string Dom::nameOf(InstanceId id) const {
    const Variant* value = getProperty(id, "Name");
    if (value == nullptr) {
        return std::string();
    }
    if (const std::string* str = std::get_if<std::string>(value)) {
        return *str;
    }
    return std::string();
}

std::vector<InstanceId> Dom::postOrder() const {
    std::vector<InstanceId> result;
    result.reserve(instances_.size());

    // Each stack frame is an instance together with how many of its children
    // have already been pushed. When all children are pushed, the instance
    // itself is emitted.
    std::vector<std::pair<InstanceId, std::size_t>> stack;
    stack.reserve(instances_.size());

    for (InstanceId root : roots_) {
        stack.emplace_back(root, 0);
        while (!stack.empty()) {
            InstanceId id = stack.back().first;
            std::size_t childIndex = stack.back().second;
            const auto& children = instances_[id].children;
            if (childIndex < children.size()) {
                InstanceId next = children[childIndex];
                stack.back().second = childIndex + 1;
                stack.emplace_back(next, 0);
            } else {
                result.push_back(id);
                stack.pop_back();
            }
        }
    }

    return result;
}

}  // namespace rbxl
