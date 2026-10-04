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

	[[nodiscard]] ImU32 ViewportTransformAxisColor(int axis);

	// G/R/S transform gizmo for one viewport's scene selection. Returns true while it owns the
	// frame's mouse, so the caller can suppress its own picking underneath the handles.
	//
	// A free function rather than a RendererPanel member because a viewport is not always a
	// RendererPanel window: the three-pane structure creation window draws its own, and without this
	// its panes had a selection but no way to move it.
	[[nodiscard]] bool RenderTransformGizmo(
		RendererWindowState &windowState, const ImVec2 &imageOrigin, const ImVec2 &imageSize, bool hovered,
		RendererLayer &layer, const WeakRef<CommandRegistry> &commandRegistryRef);
} // namespace DefectStudio
