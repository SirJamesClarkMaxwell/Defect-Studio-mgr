#pragma once

#include <cstddef>
#include <string>

#include <glm/glm.hpp>

namespace DefectStudio
{
	struct RendererWindowState;

	struct DrawSelectionDescription
	{
		std::string menuLabel;
		std::string lineLabel;
		std::string arrowLabel;
		std::string planeLabel;
		std::string segmentTooltip;
		std::string planeTooltip;
		bool canDrawSegment = false;
		bool canDrawPlane = false;
	};

	// Pure, ImGui-free description of the Rysuj gate. The visible labels carry both the atom count
	// seen by the gate and the unmet requirement, so a disabled action explains itself.
	[[nodiscard]] DrawSelectionDescription DescribeDrawSelection(std::size_t validSelectedAtomCount);
	[[nodiscard]] DrawSelectionDescription DescribeDrawSelection(const RendererWindowState &windowState);

	// Shared Rysuj item blocks. Kept beside DrawOrbitalAddMenu so the later vertical-toolbar round
	// can call the same creation paths instead of copying the viewport context menu.
	void DrawSegmentAddItems(RendererWindowState &windowState);
	void DrawPlaneAddItem(RendererWindowState &windowState);

	// The viewport Add menu's "Orbital" submenu, in its own file because RendererPanel.cpp is far
	// past the ~500-line limit in AGENTS.md and this is the largest self-contained piece of it.
	//
	// `contextMenuWorldPosition` is where the right-click landed; `anchorOrbitalToSelection` is the
	// panel's sticky "place it on the selected atom" preference, read and written here so the
	// checkbox and the placement decision cannot disagree.
	//
	// Assumes an open ImGui menu; draws BeginMenu("Orbital")...EndMenu() and nothing else.
	void DrawOrbitalAddMenu(
		RendererWindowState &windowState,
		const glm::vec3 &contextMenuWorldPosition,
		bool &anchorOrbitalToSelection,
		bool drawSubmenu = true);
} // namespace DefectStudio
