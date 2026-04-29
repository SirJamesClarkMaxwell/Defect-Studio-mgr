#pragma once

#include <optional>
#include <string>
#include <utility>
#include <variant>

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
    class Result
    {
    public:
        Result(const T &value)
            : m_State(value)
        {
        }

        Result(T &&value)
            : m_State(std::move(value))
        {
        }

        Result(const StructuredError &error)
            : m_State(error)
        {
        }

        Result(StructuredError &&error)
            : m_State(std::move(error))
        {
        }

        [[nodiscard]] bool HasValue() const noexcept
        {
            return std::holds_alternative<T>(m_State);
        }

        [[nodiscard]] explicit operator bool() const noexcept
        {
            return HasValue();
        }

        [[nodiscard]] T &Value() &
        {
            return std::get<T>(m_State);
        }

        [[nodiscard]] const T &Value() const &
        {
            return std::get<T>(m_State);
        }

        [[nodiscard]] T &&Value() &&
        {
            return std::get<T>(std::move(m_State));
        }

        [[nodiscard]] StructuredError &Error() &
        {
            return std::get<StructuredError>(m_State);
        }

        [[nodiscard]] const StructuredError &Error() const &
        {
            return std::get<StructuredError>(m_State);
        }

    private:
        std::variant<T, StructuredError> m_State;
    };

    template <>
    class Result<void>
    {
    public:
        Result() = default;

        Result(const StructuredError &error)
            : m_Error(error)
        {
        }

        Result(StructuredError &&error)
            : m_Error(std::move(error))
        {
        }

        [[nodiscard]] bool HasValue() const noexcept
        {
            return !m_Error.has_value();
        }

        [[nodiscard]] explicit operator bool() const noexcept
        {
            return HasValue();
        }

        [[nodiscard]] StructuredError &Error() &
        {
            return *m_Error;
        }

        [[nodiscard]] const StructuredError &Error() const &
        {
            return *m_Error;
        }

    private:
        std::optional<StructuredError> m_Error;
    };

    using VoidResult = Result<void>;
}
