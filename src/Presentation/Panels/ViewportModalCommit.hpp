#pragma once

#include <string_view>

#include "Core/Utils/Memory.hpp"
#include "Renderer/Scene/SceneTransform.hpp"

namespace DefectStudio
{
	class CommandRegistry;
	class RendererLayer;
	struct RendererWindowState;

	void ResetViewportModalTransform(RendererWindowState &windowState);
	[[nodiscard]] bool CommitViewportModalTransform(RendererWindowState &windowState, RendererLayer &layer,
		const WeakRef<CommandRegistry> &commands, std::string_view description);
	[[nodiscard]] bool ConfirmViewportModalTransform(RendererWindowState &windowState, RendererLayer &layer,
		const WeakRef<CommandRegistry> &commands, const SceneTransformDelta &delta, std::string_view description);
	void BeginInPlaceDuplicateMove(RendererWindowState &windowState, RendererLayer &layer,
		const WeakRef<CommandRegistry> &commands, const glm::vec2 &mouse);
	[[nodiscard]] bool RepeatViewportModalTransform(RendererWindowState &windowState, RendererLayer &layer,
		const WeakRef<CommandRegistry> &commands);
} // namespace DefectStudio
