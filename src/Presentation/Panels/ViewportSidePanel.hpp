#pragma once

#include <glm/glm.hpp>
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
	//
	// Returns the screen rectangle it occupied, grip included, or an empty one when the panel is
	// hidden. The viewport's click handling needs it: the panel is drawn after the image, so a press
	// that lands on the panel or its six-pixel resize grip also reaches the picker underneath and
	// clears the selection.
	//
	//   task/41 S11l: this is the "publish the panel's rect into the pick mask" the ponytail comment
	//   in the .cpp asked for. The first attempt inferred the rect from ImGui::GetItemRectMin/Max at
	//   the call site instead - the last item submitted before the click test - which silently stopped
	//   being the panel the moment the draw order changed in the same commit. A rect that is returned
	//   cannot drift out of sync with the thing that drew it.
	struct ViewportSidePanelRect
	{
		glm::vec2 min{0.0f};
		glm::vec2 max{0.0f};

		[[nodiscard]] bool IsEmpty() const
		{
			return !(max.x > min.x && max.y > min.y);
		}

		[[nodiscard]] bool Contains(const glm::vec2 &point) const
		{
			return !IsEmpty() && point.x >= min.x && point.x <= max.x && point.y >= min.y && point.y <= max.y;
		}
	};

	ViewportSidePanelRect DrawViewportSidePanel(
		RendererWindowState &windowState, const ImVec2 &imageOrigin, const ImVec2 &viewportSize, float deltaTime,
		RendererLayer &layer, const WeakRef<CommandRegistry> &commandRegistry, const WeakRef<DomainLayer> &domainLayer);
} // namespace DefectStudio
