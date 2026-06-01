#include "Core/dspch.hpp"

#include "Domain/Crystal/ElementProperties.hpp"

namespace DefectStudio
{
	void ElementPropertiesTable::ReplaceData(std::unordered_map<std::string, ElementProperties> data)
	{
		m_Data = std::move(data);
	}

	void ElementPropertiesTable::Clear()
	{
		m_Data.clear();
	}

	const ElementProperties &ElementPropertiesTable::Get(const std::string &symbol) const
	{
		const auto found = m_Data.find(symbol);
		if (found != m_Data.end())
			return found->second;
		return m_Fallback;
	}

	float ElementPropertiesTable::CovalentRadius(const std::string &symbol) const
	{
		return Get(symbol).covalentRadius;
	}

	bool ElementPropertiesTable::Empty() const
	{
		return m_Data.empty();
	}

	std::size_t ElementPropertiesTable::Size() const
	{
		return m_Data.size();
	}
} // namespace DefectStudio
