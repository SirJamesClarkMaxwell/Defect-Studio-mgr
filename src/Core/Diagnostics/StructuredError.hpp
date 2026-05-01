#pragma once

#include <optional>
#include <string>
#include <utility>
#include <variant>

namespace DefectStudio
{
	struct Notification;

	enum class ErrorCategory
	{
		Config,
		Job,
		IO,
		Python,
		Validation,
		Internal,
		Capability,
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

	enum class DisplayPolicy
	{
		Silent,
		Notification,
		BlockingPopup
	};

	using ErrorSeverity = Severity;

	struct StructuredError
	{
		ErrorCategory category = ErrorCategory::Runtime;
		Severity severity = Severity::Error;
		DisplayPolicy displayPolicy = DisplayPolicy::Notification;
		std::string code;
		std::string technicalDetails;
		std::string userMessage;
		std::string suggestion;
		std::string source;

		StructuredError() = default;

		StructuredError(
			ErrorCategory newCategory,
			Severity newSeverity,
			std::string newUserMessage,
			std::string newTechnicalDetails,
			std::string newSuggestion,
			std::string newSource = {},
			std::string newCode = {},
			DisplayPolicy newDisplayPolicy = DisplayPolicy::Notification)
			: category(newCategory),
			  severity(newSeverity),
			  displayPolicy(newDisplayPolicy),
			  code(std::move(newCode)),
			  technicalDetails(std::move(newTechnicalDetails)),
			  userMessage(std::move(newUserMessage)),
			  suggestion(std::move(newSuggestion)),
			  source(std::move(newSource))
		{
		}

		[[nodiscard]] bool IsRecoverable() const noexcept
		{
			return severity != Severity::Fatal;
		}
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

		[[nodiscard]] bool has_value() const noexcept
		{
			return HasValue();
		}

		[[nodiscard]] operator bool() const noexcept
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

		[[nodiscard]] T &value() &
		{
			return Value();
		}

		[[nodiscard]] const T &value() const &
		{
			return Value();
		}

		[[nodiscard]] T &&value() &&
		{
			return std::move(*this).Value();
		}

		[[nodiscard]] T *operator->()
		{
			return &Value();
		}

		[[nodiscard]] const T *operator->() const
		{
			return &Value();
		}

		[[nodiscard]] T &operator*() &
		{
			return Value();
		}

		[[nodiscard]] const T &operator*() const &
		{
			return Value();
		}

		[[nodiscard]] auto size() const requires requires(const T &value) { value.size(); }
		{
			return Value().size();
		}

		[[nodiscard]] auto front() requires requires(T &value) { value.front(); }
		{
			return Value().front();
		}

		[[nodiscard]] auto front() const requires requires(const T &value) { value.front(); }
		{
			return Value().front();
		}

		[[nodiscard]] auto begin() requires requires(T &value) { value.begin(); }
		{
			return Value().begin();
		}

		[[nodiscard]] auto begin() const requires requires(const T &value) { value.begin(); }
		{
			return Value().begin();
		}

		[[nodiscard]] auto end() requires requires(T &value) { value.end(); }
		{
			return Value().end();
		}

		[[nodiscard]] auto end() const requires requires(const T &value) { value.end(); }
		{
			return Value().end();
		}

		[[nodiscard]] StructuredError &Error() &
		{
			return std::get<StructuredError>(m_State);
		}

		[[nodiscard]] const StructuredError &Error() const &
		{
			return std::get<StructuredError>(m_State);
		}

		[[nodiscard]] StructuredError &error() &
		{
			return Error();
		}

		[[nodiscard]] const StructuredError &error() const &
		{
			return Error();
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

		[[nodiscard]] bool has_value() const noexcept
		{
			return HasValue();
		}

		[[nodiscard]] operator bool() const noexcept
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

		[[nodiscard]] StructuredError &error() &
		{
			return Error();
		}

		[[nodiscard]] const StructuredError &error() const &
		{
			return Error();
		}

	private:
		std::optional<StructuredError> m_Error;
	};

	using VoidResult = Result<void>;

	[[nodiscard]] Notification ToNotification(const StructuredError &error);
} // namespace DefectStudio
