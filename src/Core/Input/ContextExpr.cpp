#include "Core/dspch.hpp"

#include "Core/Input/ContextExpr.hpp"

#include <algorithm>
#include <cctype>
#include <utility>
#include <vector>

#include "Core/Input/ContextManager.hpp"

namespace DefectStudio
{
	[[nodiscard]] static std::string trim(std::string value)
	{
		const auto first = std::find_if_not(value.begin(), value.end(), [](unsigned char ch) {
			return std::isspace(ch) != 0;
		});
		const auto last = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char ch) {
			return std::isspace(ch) != 0;
		}).base();

		if (first >= last)
			return {};
		return std::string(first, last);
	}

	[[nodiscard]] static std::vector<std::string> splitAndExpression(const std::string &expression)
	{
		std::vector<std::string> tokens;
		std::size_t start = 0;
		while (start < expression.size())
		{
			const std::size_t next = expression.find("&&", start);
			const std::size_t end = next == std::string::npos ? expression.size() : next;
			tokens.push_back(trim(expression.substr(start, end - start)));
			if (next == std::string::npos)
				break;
			start = next + 2;
		}
		return tokens;
	}

	ContextExpr::ContextExpr(std::string expression)
		: m_Expression(trim(std::move(expression)))
	{
	}

	bool ContextExpr::Matches(const ContextManager &contextManager) const
	{
		if (m_Expression.empty())
			return true;

		const std::vector<std::string> tokens = splitAndExpression(m_Expression);
		for (std::string token : tokens)
		{
			if (token.empty())
				continue;

			bool negated = false;
			if (token.front() == '!')
			{
				negated = true;
				token = trim(token.substr(1));
			}

			const bool active = contextManager.IsActive(token);
			if (negated == active)
				return false;
		}
		return true;
	}

	const std::string &ContextExpr::GetExpression() const noexcept
	{
		return m_Expression;
	}

	bool ContextExpr::Empty() const noexcept
	{
		return m_Expression.empty();
	}
} // namespace DefectStudio
