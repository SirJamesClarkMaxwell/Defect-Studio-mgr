#include "Core/dspch.hpp"

#include "Renderer/AtomStyleTable.hpp"

namespace DefectStudio
{
	void AtomStyleTable::ReplaceStyles(
		std::unordered_map<std::string, AtomRenderStyle> styles,
		VacancyRenderStyle vacancyStyle)
	{
		*m_Styles = std::move(styles);
		*m_VacancyStyle = std::move(vacancyStyle);
	}

	void AtomStyleTable::Clear()
	{
		m_Styles->clear();
	}

	const AtomRenderStyle &AtomStyleTable::GetStyle(const std::string &symbol) const
	{
		const auto found = m_Styles->find(symbol);
		if (found != m_Styles->end())
			return found->second;
		return *m_FallbackStyle;
	}

	const VacancyRenderStyle &AtomStyleTable::GetVacancyStyle() const
	{
		return *m_VacancyStyle;
	}

	glm::vec3 AtomStyleTable::Color(const std::string &symbol) const
	{
		return GetStyle(symbol).color;
	}

	float AtomStyleTable::DisplayRadius(const std::string &symbol) const
	{
		return GetStyle(symbol).displayRadius;
	}

	std::size_t AtomStyleTable::Size() const
	{
		return m_Styles->size();
	}
} // namespace DefectStudio
