#pragma once

#include <string>

namespace DefectStudio
{
	class ContextManager;

	class ContextExpr
	{
	public:
		ContextExpr() = default;
		explicit ContextExpr(std::string expression);

		[[nodiscard]] bool Matches(const ContextManager &contextManager) const;
		[[nodiscard]] const std::string &GetExpression() const noexcept;
		[[nodiscard]] bool Empty() const noexcept;

	private:
		std::string m_Expression;
	};
} // namespace DefectStudio
