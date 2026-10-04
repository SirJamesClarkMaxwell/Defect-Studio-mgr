#include "Core/dspch.hpp"

#include "Presentation/Panels/ViewportSelection.hpp"

#include <algorithm>

#include "Renderer/RendererLayer.hpp"
#include "Renderer/RendererViewCamera.hpp"
#include "Renderer/RendererWindowState.hpp"
#include "Renderer/Scene/SceneSystem.hpp"

namespace DefectStudio
{
	// Click-select + drag-to-nudge for pinned measurement labels only (mouse path) - the keyboard
	// shortcuts that used to live in this function moved to HandlePinnedMeasurementKeyboardShortcuts
	// above, which the caller runs unconditionally every frame; this half still short-circuits behind
	// gizmoCapturing (see Render()) so a click already claimed by the atom/label transform gizmo can't
	// also be reinterpreted here as a pin pick/drag-start.
	bool HandlePinnedMeasurementInteraction(
		RendererWindowState &windowState, const ImVec2 &imageOrigin, const ImVec2 &imageSize, bool hovered)
	{
		if (!windowState.pickLabels)
			return false;

		if (windowState.camera == nullptr)
			return false;

		const glm::mat4 viewProjection = windowState.camera->ProjectionMatrix() * windowState.camera->ViewMatrix();
		auto projectToScreen = [&](const glm::vec3 &world, glm::vec2 &outScreen) -> bool {
			const glm::vec4 clip = viewProjection * glm::vec4(world, 1.0f);
			if (clip.w <= 0.0001f)
				return false;
			const glm::vec3 ndc = glm::vec3(clip) / clip.w;
			outScreen = glm::vec2(
				imageOrigin.x + (ndc.x * 0.5f + 0.5f) * imageSize.x,
				imageOrigin.y + (1.0f - (ndc.y * 0.5f + 0.5f)) * imageSize.y);
			return true;
		};

		const glm::mat4 view = windowState.camera->ViewMatrix();
		const glm::vec3 cameraRight(view[0][0], view[1][0], view[2][0]);
		const glm::vec3 cameraUp(view[0][1], view[1][1], view[2][1]);

		// Resolves the same anchor point renderLabels() draws the pin at (bond midpoint / angle
		// vertex), offset already applied - ignores the bond's periodic-image shift for a 2-atom
		// pin (only matters for bonds crossing a periodic cell boundary), fine for a hit-test.
		auto resolveAnchor = [&](const RendererWindowState::PinnedMeasurement &pin, glm::vec3 &outAnchor) -> bool {
			const bool inRange = std::all_of(pin.atomIndices.begin(), pin.atomIndices.end(), [&](const std::size_t index) {
				return index < windowState.structure.atoms.size();
			});
			if (!inRange)
				return false;
			if (pin.atomIndices.size() == 2)
			{
				outAnchor = (windowState.structure.atoms[pin.atomIndices[0]].cartesianPosition +
								windowState.structure.atoms[pin.atomIndices[1]].cartesianPosition) *
					0.5f;
			}
			else if (pin.atomIndices.size() == 3)
			{
				const std::size_t vertexIndex = ResolveAngleVertexIndex(windowState.structure, pin.atomIndices);
				outAnchor = windowState.structure.atoms[vertexIndex].cartesianPosition;
			}
			else
			{
				return false;
			}
			outAnchor += pin.worldOffset;
			return true;
		};

		if (windowState.pinnedMeasurementDragging)
		{
			if (!ImGui::IsMouseDown(ImGuiMouseButton_Left) || windowState.selectedPinnedMeasurements.empty())
			{
				windowState.pinnedMeasurementDragging = false;
				return false;
			}

			// Uses the most-recently-selected pin (back()) purely as the reference point for
			// converting screen-pixel mouse movement into a world-space delta - the SAME resulting
			// delta then applies to every selected pin's worldOffset below (rigid group drag).
			const std::size_t referenceIndex = AnnotationIndex(windowState.pinnedMeasurements, windowState.selectedPinnedMeasurements.back());
			if (referenceIndex >= windowState.pinnedMeasurements.size())
			{
				windowState.pinnedMeasurementDragging = false;
				return false;
			}
			glm::vec3 anchor(0.0f);
			if (resolveAnchor(windowState.pinnedMeasurements[referenceIndex], anchor))
			{
				glm::vec2 rightProbe, upProbe, anchorScreen;
				if (projectToScreen(anchor, anchorScreen) && projectToScreen(anchor + cameraRight, rightProbe) &&
					projectToScreen(anchor + cameraUp, upProbe))
				{
					const float pixelsPerWorldRight = std::max(1.0f, glm::length(rightProbe - anchorScreen));
					const float pixelsPerWorldUp = std::max(1.0f, glm::length(upProbe - anchorScreen));
					const glm::vec2 mousePos(ImGui::GetMousePos().x, ImGui::GetMousePos().y);
					const glm::vec2 deltaPixels = mousePos - windowState.pinnedMeasurementDragLastMouse;
					// screen Y is flipped vs cameraUp - same convention as the gizmo axis drag below.
					const glm::vec3 worldDelta = cameraRight * (deltaPixels.x / pixelsPerWorldRight) -
						cameraUp * (deltaPixels.y / pixelsPerWorldUp);
					for (const SceneObjectId id : windowState.selectedPinnedMeasurements)
					{
						const std::size_t pinIndex = AnnotationIndex(windowState.pinnedMeasurements, id);
						if (pinIndex < windowState.pinnedMeasurements.size())
							windowState.pinnedMeasurements[pinIndex].worldOffset += worldDelta;
					}
					windowState.pinnedMeasurementDragLastMouse = mousePos;
				}
			}
			return true;
		}

		if (!hovered || !ImGui::IsMouseClicked(ImGuiMouseButton_Left))
			return false;

		const bool additive = ImGui::GetIO().KeyCtrl;
		const glm::vec2 mousePos(ImGui::GetMousePos().x, ImGui::GetMousePos().y);
		// An unpinned/undragged label's anchor sits exactly at its bond's midpoint (or angle's
		// vertex) - this pick test runs before HandleViewportPick's own atom/bond hit-test and
		// unconditionally consumes the click if it hits (see gizmoCapturing in Render()), so a
		// generous radius here made every bond carrying a label nearly impossible to click as a bond:
		// any click within it always won the label instead, however much closer the bond itself was.
		// Was 40px; still comfortably clickable but no longer swallows most of the bond around it.
		constexpr float kPickRadius = 16.0f;
		int hitIndex = -1;
		float bestDistance = kPickRadius;
		for (std::size_t i = 0; i < windowState.pinnedMeasurements.size(); ++i)
		{
			if (const auto inside = MouseInLabelQuad(
					windowState, windowState.labelPickQuads.pinned, i, imageOrigin, imageSize, mousePos))
			{
				if (*inside && bestDistance > 0.0f)
				{
					bestDistance = 0.0f;
					hitIndex = static_cast<int>(i);
				}
				continue;
			}
			glm::vec3 anchor(0.0f);
			glm::vec2 anchorScreen;
			if (!resolveAnchor(windowState.pinnedMeasurements[i], anchor) || !projectToScreen(anchor, anchorScreen))
				continue;
			const float distance = glm::length(mousePos - anchorScreen);
			if (distance < bestDistance)
			{
				bestDistance = distance;
				hitIndex = static_cast<int>(i);
			}
		}

		std::vector<SceneObjectId> &selection = windowState.selectedPinnedMeasurements;
		if (hitIndex < 0)
		{
			// Ctrl+click on empty space is a no-op (matches HandleAtomPick's additive convention) -
			// only a plain click clears the selection.
			if (!additive)
				selection.clear();
			SceneSystem::SyncLabelSelection(windowState.sceneRegistry, windowState);
			return false;
		}

		const SceneObjectId hitPin = windowState.pinnedMeasurements[static_cast<std::size_t>(hitIndex)].id;
		const auto existing = std::find(selection.begin(), selection.end(), hitPin);
		// Mutual exclusivity with free-label/arrow selection below - all three live in the same OR
		// chain in Render() and short-circuit, so a pin hit here would otherwise leave a stale
		// free-label/arrow selection in place instead of replacing it the way clicking a different
		// pin already does.
		windowState.selectedFreeLabels.clear();
		windowState.selectedSceneArrows.clear();
		windowState.selectedSceneOrbitals.clear();
		windowState.selectedVacancies.clear();
		windowState.defectFrameSelected = false;
		windowState.selectedScenePlanes.clear();
		windowState.selectedScenePaths.clear();
		if (!additive)
			SceneSystem::ClearStructureSelection(windowState.sceneRegistry, windowState);

		if (additive)
		{
			// Ctrl+click toggles selection (same convention as HandleAtomPick) but never starts a
			// drag on its own - it's a pure select/deselect gesture, same as Blender/most DCC tools;
			// dragging the resulting group needs its own separate plain click-and-hold afterward.
			if (existing != selection.end())
				selection.erase(existing);
			else
				selection.push_back(hitPin);
			SceneSystem::SyncLabelSelection(windowState.sceneRegistry, windowState);
			return true;
		}

		if (existing == selection.end())
		{
			// Plain click on an UNSELECTED pin replaces the whole selection. Plain click on one
			// that's already part of a multi-selection leaves the group as-is - same "click-and-drag
			// the group you already have" convention most DCC tools use, so a multi-select doesn't
			// collapse to one item just from grabbing it.
			selection.clear();
			selection.push_back(hitPin);
		}
		SceneSystem::SyncLabelSelection(windowState.sceneRegistry, windowState);

		PushPinnedMeasurementUndoSnapshot(windowState);
		windowState.pinnedMeasurementDragging = true;
		windowState.pinnedMeasurementDragLastMouse = mousePos;
		return true;
	}

} // namespace DefectStudio
