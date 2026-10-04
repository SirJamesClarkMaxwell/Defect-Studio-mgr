// Click, drag and keyboard handling for pinned measurements and free labels. Moved out of
// RendererPanel; see ViewportRegionSelect.cpp.
#include "Core/dspch.hpp"

#include "Presentation/Panels/ViewportSelection.hpp"
#include "Presentation/Panels/ViewportTextEditor.hpp"
#include "Renderer/Scene/SceneFreeLabelAnchors.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <functional>
#include <limits>
#include <optional>
#include <vector>

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <imgui.h>
#include <ImGuizmo.h>

#include "Core/Commands/CommandRegistry.hpp"
#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Core/Logging/Logger.hpp"
#include "Events/RendererEvents.hpp"
#include "Renderer/RendererLayer.hpp"
#include "Renderer/RendererViewCamera.hpp"
#include "Renderer/RendererWindowState.hpp"
#include "Renderer/Scene/SceneComponents.hpp"
#include "Renderer/Scene/SceneSystem.hpp"
#include "Presentation/Panels/SceneArrowEditorWidget.hpp"
#include "Presentation/Panels/SceneObjectEditActions.hpp"
#include "Presentation/Panels/ScenePathEditCommands.hpp"
#include "Presentation/Panels/SceneOrbitalEditorWidget.hpp"
#include "Renderer/Scene/SelectionHitTest.hpp"

namespace DefectStudio
{
	// Whether `mouse` lies inside label `index` as it was last drawn (labels.vert's billboard
	// math on the recorded rect, projected to the viewport image). nullopt when that label has
	// no recorded quad - not drawn last frame, or drawn by the all-bonds pass - so the caller
	// falls back to its anchor-radius test.
	[[nodiscard]] std::optional<bool> MouseInLabelQuad(const RendererWindowState &windowState,
		const std::vector<LabelPickQuad> &quads, std::size_t index, const ImVec2 &imageOrigin,
		const ImVec2 &imageSize, const glm::vec2 &mouse)
	{
		if (index >= quads.size() || !quads[index].valid || windowState.camera == nullptr)
			return std::nullopt;
		const LabelPickQuad &quad = quads[index];
		const glm::mat4 view = windowState.camera->ViewMatrix();
		const glm::mat4 viewProjection = windowState.camera->ProjectionMatrix() * view;
		const glm::vec3 right(view[0][0], view[1][0], view[2][0]);
		const glm::vec3 up(view[0][1], view[1][1], view[2][1]);
		const float cosR = std::cos(quad.rotation);
		const float sinR = std::sin(quad.rotation);
		const glm::vec2 local[4] = {
			quad.min, {quad.max.x, quad.min.y}, quad.max, {quad.min.x, quad.max.y}};
		glm::vec2 screen[4];
		for (int corner = 0; corner < 4; ++corner)
		{
			const glm::vec2 p(local[corner].x * cosR - local[corner].y * sinR,
				local[corner].x * sinR + local[corner].y * cosR);
			const glm::vec4 clip =
				viewProjection * glm::vec4(quad.centre + windowState.viewOffset + right * p.x + up * p.y, 1.0f);
			if (clip.w <= 0.0001f)
				return false;
			screen[corner] = glm::vec2(imageOrigin.x + (clip.x / clip.w * 0.5f + 0.5f) * imageSize.x,
				imageOrigin.y + (0.5f - clip.y / clip.w * 0.5f) * imageSize.y);
		}
		// Convex quad: the mouse is inside when it is on the same side of all four edges.
		int sign = 0;
		for (int corner = 0; corner < 4; ++corner)
		{
			const glm::vec2 edge = screen[(corner + 1) % 4] - screen[corner];
			const glm::vec2 toMouse = mouse - screen[corner];
			const float cross = edge.x * toMouse.y - edge.y * toMouse.x;
			const int side = cross > 0.0f ? 1 : cross < 0.0f ? -1 : 0;
			if (side != 0 && sign != 0 && side != sign)
				return false;
			if (side != 0)
				sign = side;
		}
		return true;
	}

	void RegisterViewportSceneObjectCommands(CommandRegistry &registry, RendererLayer &rendererLayer)
	{
		RegisterScenePathObjectCommands(registry, rendererLayer);
	}

	// Keyboard-only shortcuts for selected scene objects: pin flip/scale, arrow clipboard actions,
	// and Delete for pins, free labels, arrows, orbitals and planes. No mouse hit-test of its own, so
	// the caller runs this unconditionally every frame rather than folding it into the
	// short-circuiting interaction chain.
	void HandlePinnedMeasurementKeyboardShortcuts(
		RendererWindowState &windowState, bool hovered, RendererLayer &layer)
	{
		if (!windowState.pickLabels)
			return;
		// Path Edit Mode owns Delete and V. In particular, do not let the same Delete key remove the
		// selected path object after its selected nodes were already removed by the path editor.
		if (windowState.pathEdit.IsActive())
			return;

		// F flips every selected pin's bond-aligned label 180 degrees (item 5) - independent of
		// hover/drag state below since it acts on whatever is already selected, not the cursor.
		// TODO: also expose as a toolbar button once one exists (see toolbar proposal).
		const bool pinSelected = !windowState.selectedPinnedMeasurements.empty();
		if (pinSelected && hovered && ImGui::IsKeyPressed(ImGuiKey_F, false))
		{
			PushPinnedMeasurementUndoSnapshot(windowState);
			for (const SceneObjectId id : windowState.selectedPinnedMeasurements)
			{
				const std::size_t pinIndex = AnnotationIndex(windowState.pinnedMeasurements, id);
				if (pinIndex < windowState.pinnedMeasurements.size())
					windowState.pinnedMeasurements[pinIndex].flipped ^= true;
			}
		}
		// Delete removes every selected pin - M/Shift+M are add-only (a bulk press over a growing
		// selection used to also silently unpin anything already pinned within it, which made
		// "select more, press M again" an unpredictable mix of adding and removing), so this is now
		// the only way to unpin a label; click (or box/circle-select) to select it/them first.
		if (pinSelected && hovered && ImGui::IsKeyPressed(ImGuiKey_Delete, false))
		{
			PushPinnedMeasurementUndoSnapshot(windowState);
			for (const SceneObjectId id : windowState.selectedPinnedMeasurements)
				windowState.pinnedMeasurements.erase(std::remove_if(windowState.pinnedMeasurements.begin(), windowState.pinnedMeasurements.end(),
					[id](const auto &pin) { return pin.id == id; }), windowState.pinnedMeasurements.end());
			windowState.selectedPinnedMeasurements.clear();
			SceneSystem::SyncLabelEntities(windowState.sceneRegistry, windowState);
		}
		// Ctrl+Shift+>/< steps every selected pin's size (the same RendererWindowState::
		// PinnedMeasurement::scale the label transform gizmo's Scale handle drags) by a fixed
		// increment - a quicker alternative to dragging that handle when only a nudge is needed. > / <
		// are Shift+Period/Shift+Comma on a standard layout, so this checks the base keys plus
		// KeyShift explicitly rather than relying on ImGui to resolve the shifted glyph.
		if (pinSelected && hovered && ImGui::GetIO().KeyCtrl && ImGui::GetIO().KeyShift)
		{
			constexpr float kScaleStep = 0.1f;
			constexpr float kMinScale = 0.2f;
			constexpr float kMaxScale = 5.0f;
			if (ImGui::IsKeyPressed(ImGuiKey_Period, false))
			{
				PushPinnedMeasurementUndoSnapshot(windowState);
				for (const SceneObjectId id : windowState.selectedPinnedMeasurements)
				{
					const std::size_t pinIndex = AnnotationIndex(windowState.pinnedMeasurements, id);
					if (pinIndex < windowState.pinnedMeasurements.size())
					{
						float &scale = windowState.pinnedMeasurements[pinIndex].style.scale;
						scale = std::min(kMaxScale, scale + kScaleStep);
					}
				}
			}
			else if (ImGui::IsKeyPressed(ImGuiKey_Comma, false))
			{
				PushPinnedMeasurementUndoSnapshot(windowState);
				for (const SceneObjectId id : windowState.selectedPinnedMeasurements)
				{
					const std::size_t pinIndex = AnnotationIndex(windowState.pinnedMeasurements, id);
					if (pinIndex < windowState.pinnedMeasurements.size())
					{
						float &scale = windowState.pinnedMeasurements[pinIndex].style.scale;
						scale = std::max(kMinScale, scale - kScaleStep);
					}
				}
			}
		}

		// Delete removes every selected free label - same rationale as the pin Delete above.
		const bool freeLabelSelected = !windowState.selectedFreeLabels.empty();
		if (freeLabelSelected && hovered && ImGui::IsKeyPressed(ImGuiKey_Delete, false))
			ExecuteSceneObjectEditAction(
				windowState, SceneObjectEditKind::FreeLabel, SceneObjectEditAction::Delete);

		// Delete removes every selected scene arrow - same rationale as the pin/free-label Delete above.
		const bool sceneArrowSelected = !windowState.selectedSceneArrows.empty();
		if (sceneArrowSelected && hovered && ImGui::IsKeyPressed(ImGuiKey_Delete, false))
			ExecuteSceneObjectEditAction(
				windowState, SceneObjectEditKind::Arrow, SceneObjectEditAction::Delete);

		const bool sceneOrbitalSelected = !windowState.selectedSceneOrbitals.empty();
		if (sceneOrbitalSelected && hovered && ImGui::IsKeyPressed(ImGuiKey_Delete, false))
			ExecuteSceneObjectEditAction(
				windowState, SceneObjectEditKind::Orbital, SceneObjectEditAction::Delete);

		const bool scenePlaneSelected = !windowState.selectedScenePlanes.empty();
		if (scenePlaneSelected && hovered && ImGui::IsKeyPressed(ImGuiKey_Delete, false))
			ExecuteSceneObjectEditAction(
				windowState, SceneObjectEditKind::Plane, SceneObjectEditAction::Delete);

		const bool scenePathSelected = !windowState.selectedScenePaths.empty();
		if (scenePathSelected && hovered && ImGui::IsKeyPressed(ImGuiKey_Delete, false))
			ExecuteSceneObjectEditAction(
				windowState, SceneObjectEditKind::Path, SceneObjectEditAction::Delete);

		// Ctrl+C/V/D for scene drawings - same "raw ImGui key check, bypass CoreLayer entirely" shape as
		// Delete just above. renderer.selection.copy/paste/duplicate (CoreLayer-dispatched, bound to the
		// same chords) only ever touch atoms - there's no fallback chain in CoreLayer::dispatchKeyChord
		// to make them "also try annotations", so this runs independently alongside the atom command.
		const bool ctrlHeld = ImGui::GetIO().KeyCtrl;
		std::optional<SceneObjectEditKind> selectedDrawingKind = ResolveSelectedDrawingKind(windowState);
		if (selectedDrawingKind == SceneObjectEditKind::Arrow)
			selectedDrawingKind.reset(); // legacy clipboard creation is unreachable after S15
		if (selectedDrawingKind.has_value() && hovered && ctrlHeld && ImGui::IsKeyPressed(ImGuiKey_C, false))
			ExecuteSceneObjectEditAction(windowState, *selectedDrawingKind, SceneObjectEditAction::Copy);
		if (hovered && ctrlHeld && ImGui::IsKeyPressed(ImGuiKey_V, false))
		{
			// A selected plane/orbital chooses
			// its own clipboard, avoiding an ambiguous paste when several kind-specific clipboards exist.
			ExecuteSceneObjectEditAction(
				windowState, selectedDrawingKind.value_or(SceneObjectEditKind::Path),
				SceneObjectEditAction::Paste);
		}
		if (selectedDrawingKind.has_value() && hovered && ctrlHeld && ImGui::IsKeyPressed(ImGuiKey_D, false))
			ExecuteSceneObjectEditAction(windowState, *selectedDrawingKind, SceneObjectEditAction::Duplicate);

		const bool altHeld = ImGui::GetIO().KeyAlt;
		const bool shiftHeld = ImGui::GetIO().KeyShift;
		const bool labelSelected = pinSelected || freeLabelSelected;
		if (labelSelected && !sceneArrowSelected && hovered && altHeld && !shiftHeld &&
			ImGui::IsKeyPressed(ImGuiKey_C, false))
		{
			const RendererWindowState::LabelStyle *style = nullptr;
			if (pinSelected)
			{
				if (const auto *pin = FindAnnotation(windowState.pinnedMeasurements, windowState.selectedPinnedMeasurements.front()))
					style = &pin->style;
			}
			else if (const auto *label = FindAnnotation(windowState.freeLabels, windowState.selectedFreeLabels.front()))
			{
				style = &label->style;
			}
			if (style != nullptr)
				CopyLabelStyle(*style);
		}
		if (labelSelected && !sceneArrowSelected && hovered && altHeld && shiftHeld &&
			ImGui::IsKeyPressed(ImGuiKey_V, false) && GetLabelStyleClipboard().has_value())
		{
			PushPinnedMeasurementUndoSnapshot(windowState);
			PasteLabelStyle(windowState, windowState.selectedPinnedMeasurements, windowState.selectedFreeLabels);
		}

	}

	std::optional<SceneObjectEditKind> ResolveSelectedDrawingKind(const RendererWindowState &windowState)
	{
		if (!windowState.selectedSceneArrows.empty())
			return SceneObjectEditKind::Arrow;
		if (!windowState.selectedSceneOrbitals.empty())
			return SceneObjectEditKind::Orbital;
		if (!windowState.selectedScenePlanes.empty())
			return SceneObjectEditKind::Plane;
		if (!windowState.selectedScenePaths.empty())
			return SceneObjectEditKind::Path;
		return std::nullopt;
	}

	// Click-select + drag-along-camera-plane for freeLabels - same shape as
	// HandlePinnedMeasurementInteraction's mouse half above, simplified: a free label's own
	// worldPosition IS the anchor (no bond/angle to resolve, no periodic offset), so this drags that
	// field directly instead of a separate worldOffset.
	bool HandleFreeLabelInteraction(
		RendererWindowState &windowState, const ImVec2 &imageOrigin, const ImVec2 &imageSize, bool hovered)
	{
		if ((!windowState.pickLabels && windowState.activeSelectionTool != SelectionToolMode::Text) || windowState.camera == nullptr)
			return false;

		if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
			windowState.freeLabelDragging = false;
		const glm::mat4 viewProjection = windowState.camera->ProjectionMatrix() * windowState.camera->ViewMatrix();
		auto projectToScreen = [&](const glm::vec3 &world, glm::vec2 &outScreen) -> bool {
			const glm::vec4 clip = viewProjection * glm::vec4(world + windowState.viewOffset, 1.0f);
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

		if (windowState.freeLabelDragging)
		{
			if (!ImGui::IsMouseDown(ImGuiMouseButton_Left) || windowState.selectedFreeLabels.empty())
			{
				windowState.freeLabelDragging = false;
				return false;
			}

			// Reference point for the pixel->world conversion only - the resulting delta applies to
			// every selected free label below (rigid group drag), same convention as the pinned
			// measurement drag above.
			const std::size_t referenceIndex = AnnotationIndex(windowState.freeLabels, windowState.selectedFreeLabels.back());
			if (referenceIndex >= windowState.freeLabels.size())
			{
				windowState.freeLabelDragging = false;
				return false;
			}
			const glm::vec3 referencePosition = windowState.freeLabels[referenceIndex].worldPosition;
			glm::vec2 anchorScreen, rightProbe, upProbe;
			if (projectToScreen(referencePosition, anchorScreen) &&
				projectToScreen(referencePosition + cameraRight, rightProbe) &&
				projectToScreen(referencePosition + cameraUp, upProbe))
			{
				const float pixelsPerWorldRight = std::max(1.0f, glm::length(rightProbe - anchorScreen));
				const float pixelsPerWorldUp = std::max(1.0f, glm::length(upProbe - anchorScreen));
				const glm::vec2 mousePos(ImGui::GetMousePos().x, ImGui::GetMousePos().y);
				const glm::vec2 deltaPixels = mousePos - windowState.freeLabelDragLastMouse;
				const glm::vec3 worldDelta = cameraRight * (deltaPixels.x / pixelsPerWorldRight) -
					cameraUp * (deltaPixels.y / pixelsPerWorldUp);
				if (glm::dot(worldDelta, worldDelta) > 0.0f && !windowState.freeLabelDragUndoPushed)
				{
					PushPinnedMeasurementUndoSnapshot(windowState);
					windowState.freeLabelDragUndoPushed = true;
				}
				for (const SceneObjectId id : windowState.selectedFreeLabels)
				{
					const std::size_t labelIndex = AnnotationIndex(windowState.freeLabels, id);
					if (labelIndex < windowState.freeLabels.size())
					{
						RendererWindowState::FreeLabel &label = windowState.freeLabels[labelIndex];
						label.worldPosition += worldDelta;
						if (const auto anchor = ResolveFreeLabelAnchor(windowState, label))
							label.anchorOffset = label.worldPosition - *anchor;
					}
				}
				windowState.freeLabelDragLastMouse = mousePos;
			}
			return true;
		}

		if (!hovered || !ImGui::IsMouseClicked(ImGuiMouseButton_Left))
			return false;

		const bool additive = ImGui::GetIO().KeyCtrl;
		const glm::vec2 mousePos(ImGui::GetMousePos().x, ImGui::GetMousePos().y);
		constexpr float kPickRadius = 16.0f;
		int hitIndex = -1;
		float bestDistance = kPickRadius;
		for (std::size_t i = 0; i < windowState.freeLabels.size(); ++i)
		{
			if (!windowState.freeLabels[i].visible)
				continue;
			if (const auto inside = MouseInLabelQuad(
					windowState, windowState.labelPickQuads.free, i, imageOrigin, imageSize, mousePos))
			{
				if (*inside && bestDistance > 0.0f)
				{
					bestDistance = 0.0f;
					hitIndex = static_cast<int>(i);
				}
				continue;
			}
			glm::vec2 labelScreen;
			if (!projectToScreen(windowState.freeLabels[i].worldPosition, labelScreen))
				continue;
			const float distance = glm::length(mousePos - labelScreen);
			if (distance < bestDistance)
			{
				bestDistance = distance;
				hitIndex = static_cast<int>(i);
			}
		}

		std::vector<SceneObjectId> &selection = windowState.selectedFreeLabels;
		if (hitIndex < 0)
		{
			if (!additive)
				selection.clear();
			SceneSystem::SyncLabelSelection(windowState.sceneRegistry, windowState);
			return false;
		}

		const SceneObjectId hitLabel = windowState.freeLabels[static_cast<std::size_t>(hitIndex)].id;
		const auto existing = std::find(selection.begin(), selection.end(), hitLabel);
		windowState.selectedPinnedMeasurements.clear();
		windowState.selectedSceneArrows.clear();
		windowState.selectedSceneOrbitals.clear();
		windowState.selectedVacancies.clear();
		windowState.defectFrameSelected = false;
		windowState.selectedScenePlanes.clear();
		windowState.selectedScenePaths.clear();
		if (!additive)
			SceneSystem::ClearStructureSelection(windowState.sceneRegistry, windowState);

		if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
		{
			selection = {hitLabel};
			SceneSystem::SyncLabelSelection(windowState.sceneRegistry, windowState);
			OpenViewportTextEditor(windowState, hitLabel);
			return true;
		}

		if (additive)
		{
			if (existing != selection.end())
				selection.erase(existing);
			else
				selection.push_back(hitLabel);
			SceneSystem::SyncLabelSelection(windowState.sceneRegistry, windowState);
			return true;
		}

		if (existing == selection.end())
		{
			selection.clear();
			selection.push_back(hitLabel);
		}
		SceneSystem::SyncLabelSelection(windowState.sceneRegistry, windowState);

		if (windowState.activeSelectionTool == SelectionToolMode::Text)
			return true;
		windowState.freeLabelDragUndoPushed = false;
		windowState.freeLabelDragging = true;
		windowState.freeLabelDragLastMouse = mousePos;
		return true;
	}
} // namespace DefectStudio
