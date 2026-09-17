#pragma once

#include <glm/glm.hpp>

namespace DefectStudio
{
	struct RendererWindowState;

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
		bool &anchorOrbitalToSelection);
} // namespace DefectStudio
