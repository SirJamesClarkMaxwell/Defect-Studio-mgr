// Click, drag and keyboard handling for pinned measurements and free labels. Moved out of
// RendererPanel; see ViewportRegionSelect.cpp.
#include "Core/dspch.hpp"

#include "Presentation/Panels/ViewportSelection.hpp"

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
#include "Presentation/Panels/SceneOrbitalEditorWidget.hpp"
#include "Renderer/Scene/SelectionHitTest.hpp"

namespace DefectStudio
{
	void RegisterViewportSceneObjectCommands(CommandRegistry &registry, RendererLayer &rendererLayer)
	{
		auto result = registry.Register(
			CommandMeta{
				CommandID{"renderer.scene_arrow.reverse"},
				"Renderer: Reverse selected arrows",
				"Renderer",
				"Swap the start and end of every selected scene arrow in the focused viewport.",
				{},
				CommandFlags::None},
			[rendererLayer = std::ref(rendererLayer)](CommandContext &) -> Unique<ICommand> {
				return CreateReverseSelectedSceneArrowsCommand(rendererLayer.get());
			});
		if (!result)
			DS_LOG_WARN("Reverse selected scene arrows command registration failed: {}", result.Error().technicalDetails);
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
		{
			PushPinnedMeasurementUndoSnapshot(windowState);
			for (const SceneObjectId id : windowState.selectedFreeLabels)
				windowState.freeLabels.erase(std::remove_if(windowState.freeLabels.begin(), windowState.freeLabels.end(),
					[id](const auto &label) { return label.id == id; }), windowState.freeLabels.end());
			windowState.selectedFreeLabels.clear();
			SceneSystem::SyncLabelEntities(windowState.sceneRegistry, windowState);
		}

		// Delete removes every selected scene arrow - same rationale as the pin/free-label Delete above.
		const bool sceneArrowSelected = !windowState.selectedSceneArrows.empty();
		if (sceneArrowSelected && hovered && ImGui::IsKeyPressed(ImGuiKey_Delete, false))
		{
			PushPinnedMeasurementUndoSnapshot(windowState);
			EraseSceneArrows(windowState, windowState.selectedSceneArrows);
		}

		const bool sceneOrbitalSelected = !windowState.selectedSceneOrbitals.empty();
		if (sceneOrbitalSelected && hovered && ImGui::IsKeyPressed(ImGuiKey_Delete, false))
		{
			PushPinnedMeasurementUndoSnapshot(windowState);
			EraseSceneOrbitals(windowState, windowState.selectedSceneOrbitals);
		}

		const bool scenePlaneSelected = !windowState.selectedScenePlanes.empty();
		if (scenePlaneSelected && hovered && ImGui::IsKeyPressed(ImGuiKey_Delete, false))
		{
			PushPinnedMeasurementUndoSnapshot(windowState);
			EraseScenePlanes(windowState, windowState.selectedScenePlanes);
		}

		// Ctrl+C/V/D for scene arrows - same "raw ImGui key check, bypass CoreLayer entirely" shape as
		// Delete just above. renderer.selection.copy/paste/duplicate (CoreLayer-dispatched, bound to the
		// same chords) only ever touch atoms - there's no fallback chain in CoreLayer::dispatchKeyChord
		// to make them "also try arrows", so this runs independently, same as Delete already does across
		// every RendererWindowState-only kind (pins/free labels/arrows) alongside the atom command.
		const bool ctrlHeld = ImGui::GetIO().KeyCtrl;
		if (sceneArrowSelected && hovered && ctrlHeld && ImGui::IsKeyPressed(ImGuiKey_C, false))
			CopySceneArrowsToClipboard(windowState);
		if (hovered && ctrlHeld && ImGui::IsKeyPressed(ImGuiKey_V, false))
			PasteSceneArrowsFromClipboard(windowState); // no selection required, mirrors atom Paste
		if (sceneArrowSelected && hovered && ctrlHeld && ImGui::IsKeyPressed(ImGuiKey_D, false))
			DuplicateSelectedSceneArrows(windowState);

		// Tab cycles which single point (Start -> End -> whole arrow) owns the unified transform
		// gizmo's axis triad - keyboard equivalent of clicking
		// the plain dots it draws for the inactive candidates (item 3 of the prior feedback round).
		const bool oneArrowSelected = windowState.selectedSceneArrows.size() == 1;
		if (oneArrowSelected && hovered && ImGui::IsKeyPressed(ImGuiKey_Tab, false))
		{
			using DragTarget = RendererWindowState::SceneArrowDragTarget;
			windowState.sceneArrowGizmoActiveTarget = windowState.sceneArrowGizmoActiveTarget == DragTarget::Both
				? DragTarget::Start
				: windowState.sceneArrowGizmoActiveTarget == DragTarget::Start ? DragTarget::End : DragTarget::Both;
		}

		// Geometry/Style copy-paste shortcuts - keyboard equivalents of the viewport context menu's
		// "Arrow > Copy/Paste Geometry|Style" items (same GetArrowGeometryClipboard/GetArrowStyleClipboard
		// pair, see that menu for why two independent slots instead of one tagged one). Paste Style would
		// naturally be Alt+V to mirror Copy Style's Alt+C, but Alt+V is already renderer.view.cycle_previous
		// (keybindings.yaml) - Alt+Shift+V instead. Likewise the whole-arrow Ctrl+C/V/D block above already
		// owns plain Ctrl+C/V, so these are Ctrl+Shift+C/V.
		const bool altHeld = ImGui::GetIO().KeyAlt;
		const bool shiftHeld = ImGui::GetIO().KeyShift;
		if (sceneArrowSelected && hovered && ctrlHeld && shiftHeld && ImGui::IsKeyPressed(ImGuiKey_C, false))
		{
			if (const RendererWindowState::SceneArrow *arrow =
					FindAnnotation(windowState.sceneArrows, windowState.selectedSceneArrows.front()))
				CopyArrowGeometry(arrow->style);
		}
		if (sceneArrowSelected && hovered && ctrlHeld && shiftHeld && ImGui::IsKeyPressed(ImGuiKey_V, false) &&
			GetArrowGeometryClipboard().has_value())
		{
			PushPinnedMeasurementUndoSnapshot(windowState);
			PasteArrowGeometry(windowState, windowState.selectedSceneArrows);
		}
		if (sceneArrowSelected && hovered && altHeld && !shiftHeld && ImGui::IsKeyPressed(ImGuiKey_C, false))
		{
			if (const RendererWindowState::SceneArrow *arrow =
					FindAnnotation(windowState.sceneArrows, windowState.selectedSceneArrows.front()))
				CopyArrowStyle(arrow->style);
		}
		if (sceneArrowSelected && hovered && altHeld && shiftHeld && ImGui::IsKeyPressed(ImGuiKey_V, false) &&
			GetArrowStyleClipboard().has_value())
		{
			PushPinnedMeasurementUndoSnapshot(windowState);
			PasteArrowStyle(windowState, windowState.selectedSceneArrows);
		}

		// notes.txt pt. 15 - label-style equivalent of the arrow Alt+C/Alt+Shift+V pair just above,
		// same keys since a selection is either arrows or labels in practice (viewport selection
		// modes - RendererPanelToolbar's Ctrl+1..5 - separate atoms/bonds/labels from each other, and
		// arrows aren't part of any pick mode, so this only guards against a stale multi-kind
		// selection surviving a mode switch, not a normal simultaneous pick).
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

		// Ctrl+Home sets the 3D cursor to the selected arrow's currently active gizmo point (Start/End/
		// midpoint per sceneArrowGizmoActiveTarget) - keyboard equivalent of the context menu's "3D Cursor >
		// Move to Arrow Start/End", extended to also cover the midpoint. Plain Home is already
		// renderer.orbit_left_90 (keybindings.yaml), hence Ctrl+Home instead.
		if (oneArrowSelected && hovered && ctrlHeld && ImGui::IsKeyPressed(ImGuiKey_Home, false))
		{
			Ref<EventBus> eventBus = layer.GetEventBus();
			if (eventBus != nullptr)
			{
				using DragTarget = RendererWindowState::SceneArrowDragTarget;
				const RendererWindowState::SceneArrow *arrow =
					FindAnnotation(windowState.sceneArrows, windowState.selectedSceneArrows.front());
				if (arrow == nullptr)
					return;
				const glm::vec3 position = windowState.sceneArrowGizmoActiveTarget == DragTarget::Start ? arrow->start
					: windowState.sceneArrowGizmoActiveTarget == DragTarget::End                        ? arrow->end
																						  : (arrow->start + arrow->end) * 0.5f;
				RendererEvents::Viewport::Cursor3DSetPositionRequested event;
				event.windowId = windowState.windowId;
				event.position = position;
				eventBus->Publish(event);
			}
		}
	}

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
		windowState.selectedScenePlanes.clear();
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

	// Click-select + drag-along-camera-plane for freeLabels - same shape as
	// HandlePinnedMeasurementInteraction's mouse half above, simplified: a free label's own
	// worldPosition IS the anchor (no bond/angle to resolve, no periodic offset), so this drags that
	// field directly instead of a separate worldOffset.
	bool HandleFreeLabelInteraction(
		RendererWindowState &windowState, const ImVec2 &imageOrigin, const ImVec2 &imageSize, bool hovered)
	{
		if (!windowState.pickLabels || windowState.camera == nullptr)
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
				for (const SceneObjectId id : windowState.selectedFreeLabels)
				{
					const std::size_t labelIndex = AnnotationIndex(windowState.freeLabels, id);
					if (labelIndex < windowState.freeLabels.size())
						windowState.freeLabels[labelIndex].worldPosition += worldDelta;
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
		windowState.selectedScenePlanes.clear();
		if (!additive)
			SceneSystem::ClearStructureSelection(windowState.sceneRegistry, windowState);

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

		PushPinnedMeasurementUndoSnapshot(windowState);
		windowState.freeLabelDragging = true;
		windowState.freeLabelDragLastMouse = mousePos;
		return true;
	}
} // namespace DefectStudio
