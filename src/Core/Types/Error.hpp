#pragma once

#include <string>
#include <expected>

namespace DefectStudio
{
    enum class ErrorCategory
    {
        Validation,
        Capability,
        IO,
        Configuration,
        Runtime,
        Cancelled
    };

    enum class Severity
    {
        Info,
        Warning,
        Error,
        Fatal
    };

    struct StructuredError
    {
        ErrorCategory category = ErrorCategory::Runtime;
        Severity severity = Severity::Error;
        std::string userMessage;
        std::string technicalDetails;
        std::string suggestion;

        [[nodiscard]] bool IsRecoverable() const noexcept { return severity != Severity::Fatal; }
    };

    template <typename T>
    using Result = std::expected<T, StructuredError>;

    // Convenience alias for operations returning no value
    using VoidResult = Result<void>;
}
