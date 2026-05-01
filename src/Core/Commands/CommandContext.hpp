#pragma once

#include <any>
#include <string>
#include <typeinfo>
#include <unordered_map>
#include <utility>

#include "Core/Diagnostics/StructuredError.hpp"
#include "Core/Types/CoreIds.hpp"

namespace DefectStudio
{
	class CommandContext
	{
	public:
		CommandContext() = default;

		explicit CommandContext(ContextID contextId)
			: m_ContextId(std::move(contextId))
		{
		}

		void SetContextId(ContextID contextId)
		{
			m_ContextId = std::move(contextId);
		}

		[[nodiscard]] const ContextID &GetContextId() const noexcept
		{
			return m_ContextId;
		}

		void SetSource(std::string source)
		{
			m_Source = std::move(source);
		}

		[[nodiscard]] const std::string &GetSource() const noexcept
		{
			return m_Source;
		}

		template <typename T>
		void Set(std::string key, T value)
		{
			m_Values[std::move(key)] = std::move(value);
		}

		template <typename T>
		[[nodiscard]] T *TryGet(const std::string &key)
		{
			auto it = m_Values.find(key);
			if (it == m_Values.end())
				return nullptr;
			return std::any_cast<T>(&it->second);
		}

		template <typename T>
		[[nodiscard]] const T *TryGet(const std::string &key) const
		{
			auto it = m_Values.find(key);
			if (it == m_Values.end())
				return nullptr;
			return std::any_cast<T>(&it->second);
		}

		template <typename T>
		[[nodiscard]] Result<T> GetCopy(const std::string &key) const
		{
			const T *value = TryGet<T>(key);
			if (value == nullptr)
			{
				return StructuredError{
					ErrorCategory::Validation,
					Severity::Error,
					"Command context is missing a required value.",
					"Missing key '" + key + "' or type mismatch for requested type '" + typeid(T).name() + "'.",
					"Register the command with a factory that documents and supplies the required context value.",
					"CommandContext",
					"command.context.missing_value"};
			}
			return *value;
		}

		[[nodiscard]] bool Contains(const std::string &key) const
		{
			return m_Values.find(key) != m_Values.end();
		}

		void Remove(const std::string &key)
		{
			m_Values.erase(key);
		}

		void ClearValues()
		{
			m_Values.clear();
		}

		[[nodiscard]] std::size_t Size() const noexcept
		{
			return m_Values.size();
		}

	private:
		ContextID m_ContextId;
		std::string m_Source;
		std::unordered_map<std::string, std::any> m_Values;
	};
} // namespace DefectStudio
