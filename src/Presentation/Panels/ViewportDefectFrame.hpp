#pragma once

#include <glm/glm.hpp>
#include <imgui.h>

namespace DefectStudio
{
	class CommandRegistry;
	struct RendererWindowState;

	// The defect's local axes (CrystalStructure::defectFrame) as a Blender-style "empty": three
	// coloured axes x/y/z from the origin, drawn over the viewport image. Nothing when the structure
	// has no frame or RendererWindowState::showDefectFrame is off.
	//   ponytail: ImGui overlay, so it is not in exported images; a mesh in the overlay pass is the
	//   upgrade if figures need it.
	void DrawViewportDefectFrameOverlay(
		const RendererWindowState &windowState, const ImVec2 &imageOrigin, const ImVec2 &imageSize);

	// Whether `mouse` (screen pixels) is on the drawn axes: within 10 px of the origin or 6 px of an
	// axis line. False when the axes are absent or hidden.
	[[nodiscard]] bool IsDefectFrameUnderMouse(
		const RendererWindowState &windowState, const ImVec2 &imageOrigin, const ImVec2 &imageSize, const ImVec2 &mouse);

	// Object Properties section for the selected axes: origin (editable), axes, show toggle, flip,
	// remove. Edits go through renderer.defect_frame.set.
	void DrawSelectedDefectFrameSection(RendererWindowState &windowState, CommandRegistry *commandRegistry);

	// Scene Outliner row "Osie defektu": click selects the axes (like a viewport click), the
	// visibility columns drive showDefectFrame. Nothing when the structure has no axes.
	void DrawDefectFrameOutlinerRow(RendererWindowState &windowState);

	// "Defect axes (empty)" submenu of the Add menus (Shift+A, right-click Add). Creates or replaces
	// the axes through renderer.defect_frame.set (one undo step each):
	//   Tutaj, osie globalne         origin = `position` (click point / 3D cursor), world x/y/z
	//   z: wakans -> zaznaczenie     origin = vacancy nearest the selected atoms' centroid, z toward
	//                                that centroid (NV-: select N)
	//   z: atom 1 -> atom 2          exactly two selected atoms, origin on the first
	//   z: kursor 3D -> zaznaczenie
	//   Z osi zaznaczonego obiektu   origin and x/z of the selected arrow / plane / orbital / path
	void DrawDefectFrameAddMenu(RendererWindowState &windowState, CommandRegistry *commandRegistry, const glm::vec3 &position);

	// "Osie defektu" submenu of the viewport context menu, for existing axes: select them, align the
	// selected objects to them (one scene-objects undo step), pin the selection to them (temporary
	// parenting, RendererWindowState::defectFrameChildren), re-aim x, flip z, show, remove.
	void DrawDefectFrameMenu(RendererWindowState &windowState, CommandRegistry *commandRegistry);
} // namespace DefectStudio
