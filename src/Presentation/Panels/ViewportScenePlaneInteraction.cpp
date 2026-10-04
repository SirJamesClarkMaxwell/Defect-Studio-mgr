// Click-select for ScenePlanes. Last in the short-circuit chain: a plane is the largest thing in
// the viewport and usually has atoms, paths and orbitals in front of it, so every smaller object
// gets first refusal on the click.
#include "Core/dspch.hpp"

#include "Presentation/Panels/ViewportSelection.hpp"
#include "Presentation/Panels/ViewportPicking.hpp"

#include <algorithm>
#include <optional>

#include <imgui.h>

#include "Renderer/RendererViewCamera.hpp"
#include "Renderer/RendererWindowState.hpp"
#include "Renderer/Scene/SceneSystem.hpp"
#include "Renderer/Scene/ScenePlaneGeometry.hpp"

namespace DefectStudio
{
	// Plain click replaces the selection, Ctrl-click toggles - the same two rules as arrows and
	// orbitals. No drag: a plane is moved with the transform gizmo, and a sheet that slid away
	// under a stray click would be hard to put back where it was fitted.
	bool HandleScenePlaneInteraction(
		RendererWindowState &windowState, const ImVec2 &imageOrigin, const ImVec2 &imageSize, bool hovered)
	{
		if (!windowState.pickLabels || windowState.camera == nullptr || windowState.scenePlanes.empty())
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

		// Same unprojection as the orbital handler next door, against the drawn image rather than
		// windowState.viewportSize - the two can differ by a supersample factor.
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

		// An atom under the cursor wins: the hit volume here is far larger than the drawn shape and
		// usually contains the atom the object sits on, which would otherwise be unclickable in the
		// "All" selection mode.
		if (windowState.pickAtoms && PickAtomAlongRay(windowState, rayOrigin, rayDirection))
			return false;

		const std::optional<std::size_t> hit = PickScenePlane(windowState, rayOrigin, rayDirection);
		if (!hit)
			return false;

		const SceneObjectId id = windowState.scenePlanes[*hit].id;
		auto &selection = windowState.selectedScenePlanes;
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
			// Same as an arrow click: a leftover atom selection would also be deleted by Delete.
			SceneSystem::ClearStructureSelection(windowState.sceneRegistry, windowState);
		}

		// Claiming the click clears the other kinds, so the properties panel shows one thing.
		windowState.selectedFreeLabels.clear();
		windowState.selectedPinnedMeasurements.clear();
		windowState.selectedSceneOrbitals.clear();
		windowState.selectedVacancies.clear();
		windowState.defectFrameSelected = false;
		return true;
	}
} // namespace DefectStudio
