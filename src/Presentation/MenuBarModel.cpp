#include "Core/dspch.hpp"
#include "Presentation/MenuBarModel.hpp"

namespace DefectStudio
{
	// STUB - not implemented yet. The header and tests/Presentation/MenuBarModelTests.cpp are the
	// contract; these return neutral values so the test binary links.

	const char *PanelCategoryName(PanelCategory /*category*/)
	{
		return "";
	}

	std::vector<PanelMenuGroup> BuildPanelMenuGroups(const std::vector<PanelMenuEntry> & /*panels*/)
	{
		return {};
	}

	std::vector<CommandMenuGroup> BuildCommandMenuGroups(const std::vector<CommandMeta> & /*commands*/)
	{
		return {};
	}
} // namespace DefectStudio
