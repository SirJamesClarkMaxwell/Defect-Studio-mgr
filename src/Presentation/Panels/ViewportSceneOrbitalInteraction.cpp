// Click-select for SceneOrbitals. Sits beside ViewportSceneArrowInteraction and runs after it in
// the same short-circuit chain, so an arrow drawn through an orbital still wins the click.
#include "Core/dspch.hpp"

#include "Presentation/Panels/ViewportSelection.hpp"

#include <algorithm>
#include <optional>

#include <imgui.h>

#include "Renderer/RendererViewCamera.hpp"
#include "Renderer/RendererWindowState.hpp"
#include "Renderer/Scene/SceneOrbitalGeometry.hpp"

namespace DefectStudio
{
	// Plain click replaces the selection, Ctrl-click toggles - the same two rules every other scene
	// object in this viewport follows, so there is nothing new to learn for orbitals.
	//
	// There is deliberately no drag here. An orbital is moved through the transform gizmo like an
	// atom, not by being shoved around with the mouse: it usually sits on an atom, and a stray drag
	// that silently detached it from that atom would be the worst possible default.
	bool HandleSceneOrbitalInteraction(
		RendererWindowState &windowState, const ImVec2 &imageOrigin, const ImVec2 &imageSize, bool hovered)
	{
		if (!windowState.pickLabels || windowState.camera == nullptr || windowState.sceneOrbitals.empty())
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

		// Same unprojection as ViewportPicking's atom ray, against the panel's drawn image rather
		// than windowState.viewportSize - the two can differ by a supersample factor.
		const float ndcX = (2.0f * relativeX / imageSize.x) - 1.0f;
		const float ndcY = -((2.0f * relativeY / imageSize.y) - 1.0f);
		const glm::mat4 inverseViewProjection =
			glm::inverse(windowState.camera->ProjectionMatrix() * windowState.camera->ViewMatrix());
		const glm::vec4 nearPoint = inverseViewProjection * glm::vec4(ndcX, ndcY, -1.0f, 1.0f);
		const glm::vec4 farPoint = inverseViewProjection * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);
		if (std::abs(nearPoint.w) < 1e-6f || std::abs(farPoint.w) < 1e-6f)
			return false;
		const glm::vec3 rayOrigin = glm::vec3(nearPoint) / nearPoint.w;
		const glm::vec3 rayDirection = glm::vec3(farPoint) / farPoint.w - rayOrigin;

		const std::optional<std::size_t> hit =
			PickSceneOrbital(windowState, windowState.structure, rayOrigin, rayDirection);
		if (!hit)
			return false;

		const SceneObjectId id = windowState.sceneOrbitals[*hit].id;
		auto &selection = windowState.selectedSceneOrbitals;
		if (ImGui::GetIO().KeyCtrl)
		{
			const auto found = std::find(selection.begin(), selection.end(), id);
			if (found == selection.end())
				selection.push_back(id);
			else
				selection.erase(found);
		}
		else
		{
			selection = {id};
		}

		// Claiming the click also means clearing the other kinds' selections, so the properties
		// panel shows one thing rather than an orbital and a leftover arrow at once.
		windowState.selectedFreeLabels.clear();
		windowState.selectedSceneArrows.clear();
		windowState.selectedPinnedMeasurements.clear();
		windowState.selectedScenePlanes.clear();
		return true;
	}
} // namespace DefectStudio
