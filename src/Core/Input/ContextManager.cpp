#include "Core/dspch.hpp"

#include "Core/Input/ContextManager.hpp"

#include <algorithm>

namespace DefectStudio
{
	void ContextManager::SetActive(std::string context, bool active)
	{
		auto it = std::find(m_ActiveContexts.begin(), m_ActiveContexts.end(), context);
		if (active)
		{
			if (it == m_ActiveContexts.end())
				m_ActiveContexts.push_back(std::move(context));
			return;
		}

		if (it != m_ActiveContexts.end())
			m_ActiveContexts.erase(it);
	}

	void ContextManager::Clear()
	{
		m_ActiveContexts.clear();
	}

	bool ContextManager::IsActive(const std::string &context) const
	{
		return std::find(m_ActiveContexts.begin(), m_ActiveContexts.end(), context) != m_ActiveContexts.end();
	}
} // namespace DefectStudio
