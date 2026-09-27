#pragma once

#include <optional>

#include <glm/glm.hpp>
#include <imgui.h>

#include "Core/Utils/Memory.hpp"

namespace DefectStudio
{
	class CommandRegistry;
	class RendererLayer;
	enum class ModalTransformOp;
	struct RendererWindowState;

	void BeginViewportModalTransform(
		RendererWindowState &windowState, ModalTransformOp op, const glm::vec2 &mouse,
		std::optional<int> axis = std::nullopt, bool startedFromHandle = false, bool planeConstraint = false);

	[[nodiscard]] bool UpdateViewportModalTransform(
		RendererWindowState &windowState, const ImVec2 &imageOrigin, const ImVec2 &imageSize,
		RendererLayer &layer, const WeakRef<CommandRegistry> &commandRegistryRef);
} // namespace DefectStudio
