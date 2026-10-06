#include "Core/dspch.hpp"

#include <utility>

#include "Presentation/PanelDockRegions.hpp"

namespace DefectStudio
{
	DockRegion ClassifyDockRegion(const DockRectangle &node, const DockRectangle &central)
	{
		if (central.min.x >= central.max.x || central.min.y >= central.max.y)
			return DockRegion::Floating;

		const bool left = node.max.x <= central.min.x;
		const bool right = node.min.x >= central.max.x;
		const bool top = node.max.y <= central.min.y;
		const bool bottom = node.min.y >= central.max.y;

		const float horizontalClearance = left
			? central.min.x - node.max.x
			: right ? node.min.x - central.max.x : 0.0f;
		const float verticalClearance = top
			? central.min.y - node.max.y
			: bottom ? node.min.y - central.max.y : 0.0f;

		if (left || right || top || bottom)
		{
			if ((left || right) && (top || bottom))
				return horizontalClearance >= verticalClearance
					? (left ? DockRegion::Left : DockRegion::Right)
					: (top ? DockRegion::Top : DockRegion::Bottom);
			if (left || right)
				return left ? DockRegion::Left : DockRegion::Right;
			if (top || bottom)
				return top ? DockRegion::Top : DockRegion::Bottom;
		}

		return DockRegion::Central;
	}

	void DockRegionTracker::Observe(const std::string &title, DockRegion region)
	{
		m_Regions[title] = region;
	}

	DockRegion DockRegionTracker::RegionOf(const std::string &title) const
	{
		const auto it = m_Regions.find(title);
		return it == m_Regions.end() ? DockRegion::Floating : it->second;
	}

	DockRegionToggle::Decision DockRegionToggle::Toggle(std::vector<std::string> visibleTitles)
	{
		if (!visibleTitles.empty())
		{
			m_HiddenTitles = std::move(visibleTitles);
			return Decision{m_HiddenTitles, false};
		}

		if (m_HiddenTitles.empty())
			return {};

		Decision decision{m_HiddenTitles, true};
		m_HiddenTitles.clear();
		return decision;
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
