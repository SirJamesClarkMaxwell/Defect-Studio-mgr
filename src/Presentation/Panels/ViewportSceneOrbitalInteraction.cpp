// Click-select for scene orbitals in
// the same short-circuit chain, so an arrow drawn through an orbital still wins the click.
#include "Core/dspch.hpp"

#include "Presentation/Panels/ViewportSelection.hpp"
#include "Presentation/Panels/ViewportPicking.hpp"

#include <algorithm>
#include <cmath>
#include <optional>

#include <imgui.h>

#include "Renderer/RendererViewCamera.hpp"
#include "Renderer/RendererWindowState.hpp"
#include "Renderer/Scene/SceneSystem.hpp"
#include "Renderer/Scene/SceneOrbitalGeometry.hpp"

namespace DefectStudio
{
	// Plain click replaces the selection, Ctrl-click toggles and Shift-click adds, as other scene
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

		const auto hit = PickSceneOrbitalSurface(windowState, windowState.structure, rayOrigin, rayDirection);
		if (!hit) return false;
		// Compare drawn surfaces, not the orbital bounding sphere or the padded atom pick radius.
		const glm::vec3 direction = glm::normalize(rayDirection);
		if (windowState.pickAtoms && windowState.showAtoms)
			for (const auto &atom : windowState.structure.atoms)
			{
				if (!atom.visible) continue;
				const glm::vec3 offset = rayOrigin - atom.cartesianPosition;
				const float b = glm::dot(offset, direction);
				const float discriminant = b * b - glm::dot(offset, offset) + atom.radius * atom.radius;
				if (discriminant >= 0.0f)
				{
					const float distance = -b - std::sqrt(discriminant);
					if (distance > 0.0f && distance < hit->distance) return false;
				}
			}
		const SceneObjectId id = windowState.sceneOrbitals[hit->index].id;
		auto &selection = windowState.selectedSceneOrbitals;
		if (ImGui::GetIO().KeyCtrl || ImGui::GetIO().KeyShift)
		{
			const auto found = std::find(selection.begin(), selection.end(), id);
			if (found == selection.end())
				selection.push_back(id);
			else if (ImGui::GetIO().KeyCtrl)
				selection.erase(found);
		}
		else
		{
			selection = {id};
			// Same as an arrow click: a leftover atom selection would also be deleted by Delete.
			SceneSystem::ClearStructureSelection(windowState.sceneRegistry, windowState);
		}

		if (!ImGui::GetIO().KeyCtrl && !ImGui::GetIO().KeyShift)
		{
			// Claiming the click also means clearing the other kinds' selections, so the properties
			// panel shows one thing rather than an orbital and a leftover arrow at once.
			windowState.selectedFreeLabels.clear();
			windowState.selectedPinnedMeasurements.clear();
			windowState.selectedScenePlanes.clear();
			windowState.selectedScenePaths.clear();
			windowState.selectedVacancies.clear();
			windowState.defectFrameSelected = false;
		}
		return true;
	}
} // namespace DefectStudio
