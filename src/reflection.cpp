#include <rbxl/reflection.hpp>

namespace rbxl {

void SimpleReflectionDatabase::addService(std::string className) {
    services_.insert(std::move(className));
}

void SimpleReflectionDatabase::addDefault(std::string className, std::string propertyName,
                                           Variant value) {
    defaults_.emplace(std::make_pair(std::move(className), std::move(propertyName)),
                       std::move(value));
}

bool SimpleReflectionDatabase::isService(const std::string& className) const {
    return services_.count(className) != 0;
}

// Known when either addService or addDefault has ever mentioned the class:
// neither alone is a complete registry of what this database "knows", so
// this checks both.
bool SimpleReflectionDatabase::knowsClass(const std::string& className) const {
    if (services_.count(className) != 0) return true;
    for (const auto& [key, value] : defaults_) {
        (void)value;
        if (key.first == className) return true;
    }
    return false;
}

Variant SimpleReflectionDatabase::defaultValue(const std::string& className,
                                                const std::string& propertyName) const {
    auto it = defaults_.find(std::make_pair(className, propertyName));
    if (it == defaults_.end()) return std::monostate{};
    return it->second;
}

}  // namespace rbxl
