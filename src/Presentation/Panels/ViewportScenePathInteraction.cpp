// Click-select for ScenePaths. Sits between the orbital and the plane handler in the chain: a
// path is a thin object that a plane is usually drawn behind, so it gets first refusal on the
// click over the plane, and last refusal against everything smaller than it.
#include "Core/dspch.hpp"

#include "Presentation/Panels/ViewportSelection.hpp"

#include <algorithm>
#include <optional>

#include <imgui.h>

#include "Renderer/Path/ScenePathPicking.hpp"
#include "Renderer/RendererViewCamera.hpp"
#include "Renderer/RendererWindowState.hpp"

namespace DefectStudio
{
	// Plain click replaces the selection, Ctrl-click toggles - the same two rules as arrows,
	// orbitals and planes. No drag: S12 owns edit mode, and a path that slid away under a stray
	// click would be hard to put back.
	bool HandleScenePathInteraction(
		RendererWindowState &windowState, const ImVec2 &imageOrigin, const ImVec2 &imageSize, bool hovered)
	{
		if (!windowState.pickLabels || windowState.camera == nullptr || windowState.paths == nullptr ||
			windowState.paths->Store().Empty())
			return false;
		if (!hovered || !ImGui::IsMouseClicked(ImGuiMouseButton_Left))
			return false;
		if (imageSize.x <= 0.0f || imageSize.y <= 0.0f)
			return false;

		const ImVec2 mouse = ImGui::GetMousePos();
		const float relativeX = mouse.x - imageOrigin.x;
		const float relativeY = mouse.y - imageOrigin.y;
		if (relativeX < 0.0f || relativeY < 0.0f || relativeX > imageSize.x || relativeY > imageSize.y)
			return false;

		// Against the drawn image rather than windowState.viewportSize - the two differ by the
		// supersample factor, and the picker works in the pixels the user is actually looking at.
		const glm::mat4 view = windowState.camera->ViewMatrix();
		PathPickSettings settings;
		settings.viewProjection = windowState.camera->ProjectionMatrix() * view;
		settings.viewportSize = glm::vec2(imageSize.x, imageSize.y);
		settings.cursor = glm::vec2(relativeX, relativeY);
		// Read exactly as OpenGlPathRenderer reads it, so the pick width matches the drawn width.
		settings.cameraRight = glm::vec3(view[0][0], view[1][0], view[2][0]);
		settings.editMode = false;

		const std::optional<ScenePathPick> hit = PickFrontmostScenePath(*windowState.paths, settings);
		if (!hit)
			return false;

		auto &selection = windowState.selectedScenePaths;
		if (ImGui::GetIO().KeyCtrl)
		{
			const auto found = std::find(selection.begin(), selection.end(), hit->path);
			if (found == selection.end())
				selection.push_back(hit->path);
			else
				selection.erase(found);
		}
		else
		{
			selection = {hit->path};
		}

		// Claiming the click clears the other kinds, so the properties panel shows one thing.
		windowState.selectedFreeLabels.clear();
		windowState.selectedSceneArrows.clear();
		windowState.selectedPinnedMeasurements.clear();
		windowState.selectedSceneOrbitals.clear();
		windowState.selectedScenePlanes.clear();
		return true;
	}
} // namespace DefectStudio
