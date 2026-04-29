#pragma once

#include <string>
#include <utility>

namespace DefectStudio
{
    struct CommandID
    {
        std::string value;
        CommandID() = default;
        explicit CommandID(std::string v) : value(std::move(v)) {}
        explicit operator bool() const noexcept { return !value.empty(); }
        bool operator==(const CommandID &o) const noexcept { return value == o.value; }
    };

    struct ContextID
    {
        std::string value;
        ContextID() = default;
        explicit ContextID(std::string v) : value(std::move(v)) {}
        explicit operator bool() const noexcept { return !value.empty(); }
    };

    struct CapabilityID
    {
        std::string value;
        CapabilityID() = default;
        explicit CapabilityID(std::string v) : value(std::move(v)) {}
        explicit operator bool() const noexcept { return !value.empty(); }
    };
}
