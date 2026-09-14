#pragma once

#include <optional>
#include <string_view>

#include <glm/glm.hpp>
#include <imgui.h>

#include "Core/Utils/Memory.hpp"

namespace DefectStudio
{
	class CommandRegistry;
	enum class ModalTransformOp;
	struct RendererWindowState;

	void BeginAtomModalTransform(
		RendererWindowState &windowState, ModalTransformOp op, const glm::vec2 &mouse,
		std::optional<int> globalAxis = std::nullopt, bool startedFromHandle = false);

	[[nodiscard]] bool UpdateAtomModalTransform(
		RendererWindowState &windowState, const ImVec2 &imageOrigin, const ImVec2 &imageSize,
		const WeakRef<CommandRegistry> &commandRegistryRef);

	void CommitAtomGizmoPreview(
		RendererWindowState &windowState, const WeakRef<CommandRegistry> &commandRegistryRef,
		std::string_view description);
} // namespace DefectStudio
