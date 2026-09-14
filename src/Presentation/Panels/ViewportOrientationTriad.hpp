#pragma once

#include <array>

#include <glm/glm.hpp>
#include <imgui.h>

#include "Renderer/Scene/ModalTransform.hpp"

namespace DefectStudio
{
	struct RendererWindowState;

	using OrientationAxes = std::array<glm::vec3, 3>;
	using OrientationScreenAxes = std::array<glm::vec2, 3>;

	[[nodiscard]] OrientationAxes ResolveNormalizedOrientationAxes(
		TransformOrientation orientation, const TransformBases &bases);
	[[nodiscard]] OrientationScreenAxes ProjectOrientationAxes(
		const glm::mat4 &view, const OrientationAxes &axes);

	// Draws the shared viewport orientation indicator. Returns true only when its click toggled the
	// orientation, allowing a live modal transform to receive the same LMB as confirmation.
	[[nodiscard]] bool RenderViewportOrientationTriad(
		RendererWindowState &windowState, const ImVec2 &imageOrigin, const ImVec2 &imageSize, bool hovered);
} // namespace DefectStudio
