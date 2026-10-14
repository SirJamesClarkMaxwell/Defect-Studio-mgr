#pragma once

#include <cstddef>
#include <optional>

#include <glm/glm.hpp>

namespace DefectStudio
{
	struct RendererWindowState;
	struct ModalTransformView;

	[[nodiscard]] std::optional<std::size_t> PickViewportModalAtomAlongRay(
		const RendererWindowState &window, const glm::vec3 &origin, const glm::vec3 &direction);
	void ApplyViewportModalAtomPick(RendererWindowState &window, std::size_t atomIndex, const glm::vec2 &mouse);
	// Consumes the entire frame, including a pick or Escape, while reference picking is active.
	[[nodiscard]] bool UpdateViewportModalPicking(
		RendererWindowState &window, const ModalTransformView &view, const glm::vec2 &mouse);
} // namespace DefectStudio
