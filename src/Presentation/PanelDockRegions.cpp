#include "Core/dspch.hpp"

#include "Presentation/PanelDockRegions.hpp"

namespace DefectStudio
{
	DockRegion ClassifyDockRegion(const DockRectangle &node, const DockRectangle &central)
	{
		(void)node;
		(void)central;
		return DockRegion::Floating; // TODO(task/42): contract stub
	}

	DockRegionToggle::Decision DockRegionToggle::Toggle(std::vector<std::string> visibleTitles)
	{
		(void)visibleTitles;
		return {}; // TODO(task/42): contract stub
	}

	bool DockRegionToggle::IsHidden() const
	{
		return !m_HiddenTitles.empty();
	}

	void DockRegionToggle::Forget()
	{
		m_HiddenTitles.clear();
	}
} // namespace DefectStudio
