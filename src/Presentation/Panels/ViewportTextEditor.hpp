#pragma once

#include <cstddef>

#include <glm/glm.hpp>
#include <imgui.h>

#include "Renderer/Scene/SceneObject.hpp"

namespace DefectStudio
{
	struct RendererWindowState;

	void OpenViewportTextEditor(RendererWindowState &window, SceneObjectId label, bool created = false);
	[[nodiscard]] bool IsViewportTextEditorActive(const RendererWindowState &window);
	void DrawViewportTextEditor(RendererWindowState &window, const ImVec2 &imageOrigin, const ImVec2 &imageSize);
	void HandleViewportTextToolClick(RendererWindowState &window, const ImVec2 &imageOrigin,
		const ImVec2 &imageSize, const glm::vec3 &clickPosition);
	void DrawFreeLabelTextInput(RendererWindowState &window, std::size_t index, const char *id, float width);
	void DrawSelectedFreeLabelTextProperties(RendererWindowState &window);
} // namespace DefectStudio
