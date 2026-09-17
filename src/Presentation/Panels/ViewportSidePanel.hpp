#pragma once

#include <imgui.h>

#include "Core/Utils/Memory.hpp"

namespace DefectStudio
{
	class CommandRegistry;
	class DomainLayer;
	class RendererLayer;
	struct RendererWindowState;

	// Blender's N panel: the object properties slide in over the right edge of the viewport instead
	// of needing the docked Object Properties panel open. It floats over the rendered image - the
	// viewport is not resized - and shows the very same widgets, via DrawObjectPropertiesContent.
	//
	// Call it from inside the viewport's ImGui window, after the image has been drawn, with the
	// image's screen origin and size.
	void DrawViewportSidePanel(
		RendererWindowState &windowState, const ImVec2 &imageOrigin, const ImVec2 &viewportSize, float deltaTime,
		RendererLayer &layer, const WeakRef<CommandRegistry> &commandRegistry, const WeakRef<DomainLayer> &domainLayer);
} // namespace DefectStudio
