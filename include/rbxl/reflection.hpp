#pragma once
#include <rbxl/variant.hpp>
#include <map>
#include <set>
#include <string>
#include <utility>

// The optional seam a caller plugs a Roblox reflection database into. No
// database ships with this library: property types are read from the file
// itself, so decoding never needs one, and the library stays free of any
// Roblox version coupling. This interface exists purely so the binary
// encoder's gap-filling logic (see src/binary/encode.cpp) can ask a better
// question than "what's the zero value for this type" when one is supplied.
namespace rbxl {

// Supplied by the caller. Every method may return "unknown"; the encoder
// must behave correctly when this interface is absent entirely (a null
// `ReflectionDatabase*`), which is the only path any file format decoder
// exercises.
class ReflectionDatabase {
public:
    virtual ~ReflectionDatabase() = default;

    // True when the class is a service and needs an INST service marker.
    // Return `false` for "not a service" and use `knowsClass` to distinguish
    // that from "never heard of it". This is a fallback only: an instance
    // whose `Dom::isService` is already true keeps that regardless of what
    // this returns.
    virtual bool isService(const std::string& className) const = 0;
    virtual bool knowsClass(const std::string& className) const = 0;

    // The default value for a property, used to fill gaps when instances of
    // a class disagree about which properties they define. Returns Nil when
    // unknown, in which case the encoder falls back to a zero value.
    virtual Variant defaultValue(const std::string& className,
                                  const std::string& propertyName) const = 0;
};

// A trivial in-memory implementation, useful for tests and for callers who
// only need to teach the encoder about a handful of classes.
class SimpleReflectionDatabase : public ReflectionDatabase {
public:
    void addService(std::string className);
    void addDefault(std::string className, std::string propertyName, Variant value);

    bool isService(const std::string& className) const override;
    bool knowsClass(const std::string& className) const override;
    Variant defaultValue(const std::string& className,
                          const std::string& propertyName) const override;

private:
    std::set<std::string> services_;
    std::map<std::pair<std::string, std::string>, Variant> defaults_;
};

}  // namespace rbxl
