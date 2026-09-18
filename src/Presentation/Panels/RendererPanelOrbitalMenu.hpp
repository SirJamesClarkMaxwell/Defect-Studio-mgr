#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include <glm/glm.hpp>

namespace DefectStudio
{
	struct RendererWindowState;
	enum class OrbitalPreset;

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

	// One-centre presets become one orbital per selected atom; two-centre presets consume exactly
	// one pair. Cursor placement is represented by one empty anchor group.
	[[nodiscard]] std::vector<std::vector<std::size_t>> ResolveOrbitalAddAnchorGroups(
		OrbitalPreset preset, const std::vector<std::size_t> &selectedAtoms, bool anchorToSelection);

	// Shared Rysuj item blocks. Kept beside DrawOrbitalAddMenu so the later vertical-toolbar round
	// can call the same creation paths instead of copying the viewport context menu.
	void DrawSegmentAddItems(RendererWindowState &windowState);
	void DrawPlaneAddItem(RendererWindowState &windowState);
	void DrawFreeSegmentAddItems(RendererWindowState &windowState, const glm::vec3 &worldPosition);
	void DrawFreePlaneAddItem(RendererWindowState &windowState, const glm::vec3 &worldPosition);

	// The viewport Add menu's "Orbital" submenu, in its own file because RendererPanel.cpp is far
	// past the ~500-line limit in AGENTS.md and this is the largest self-contained piece of it.
	//
	// `worldPosition` is the right-click position or 3D cursor. Placement is chosen explicitly by
	// entering either the selected-atoms catalogue or the free-standing catalogue.
	//
	// Assumes an open ImGui menu; draws BeginMenu("Orbital")...EndMenu() and nothing else.
	void DrawOrbitalAddMenu(
		RendererWindowState &windowState,
		const glm::vec3 &worldPosition,
		bool drawSubmenu = true);
} // namespace DefectStudio
