#pragma once

#include <array>

#include <imgui.h>

#include "Core/Utils/Memory.hpp"
#include "Renderer/RendererWindowState.hpp"

namespace DefectStudio
{
	class CommandRegistry;
	class RendererLayer;
	class RendererViewCamera;

	struct SceneArrowHandleGeometry
	{
		RendererWindowState::SceneArrowDragTarget target = RendererWindowState::SceneArrowDragTarget::Both;
		glm::vec2 point = glm::vec2(0.0f);
		// Deliberately different: a small dot at the true endpoint reads clearly against the arrow's
		// own tapering tip mesh (a marker sized to match the old, larger pick radius visually
		// swallowed the tip, making the point look offset from it) - the pick radius stays generous
		// so grabbing a handle doesn't require pixel-perfect precision.
		float drawRadius = 0.0f;
		float pickRadius = 0.0f;
		bool visible = false;
	};

	// Shared screen geometry for the visible arrow markers and their hit-test. Endpoints remain visible
	// for every operation; the midpoint is a Translate-only target and is shown only for a sole arrow.
	[[nodiscard]] std::array<SceneArrowHandleGeometry, 3> BuildSceneArrowHandleGeometry(
		const RendererWindowState::SceneArrow &arrow,
		const RendererViewCamera &camera,
		const ImVec2 &imageOrigin,
		const ImVec2 &imageSize,
		GizmoOperation operation,
		bool showMidpoint,
		RendererWindowState::SceneArrowDragTarget activeTarget,
		bool active);
	void DrawSceneArrowHandleMarkers(
		const RendererWindowState &windowState, const ImVec2 &imageOrigin, const ImVec2 &imageSize);

	// G/R/S transform gizmo for one viewport's atom/label/arrow selection. Returns true while it owns the
	// frame's mouse, so the caller can suppress its own picking underneath the handles.
	//
	// A free function rather than a RendererPanel member because a viewport is not always a
	// RendererPanel window: the three-pane structure creation window draws its own, and without this
	// its panes had a selection but no way to move it.
	[[nodiscard]] bool RenderTransformGizmo(
		RendererWindowState &windowState, const ImVec2 &imageOrigin, const ImVec2 &imageSize, bool hovered,
		RendererLayer &layer, const WeakRef<CommandRegistry> &commandRegistryRef);
} // namespace DefectStudio
