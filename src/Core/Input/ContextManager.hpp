#pragma once

#include <string>
#include <vector>

namespace DefectStudio
{
	class ContextManager
	{
	public:
		void SetActive(std::string context, bool active = true);
		void Clear();

		[[nodiscard]] bool IsActive(const std::string &context) const;

	private:
		std::vector<std::string> m_ActiveContexts;
	};
} // namespace DefectStudio
