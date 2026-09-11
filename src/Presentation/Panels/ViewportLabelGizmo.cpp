// Transform gizmo for the label selection (pinned measurements + free labels). Moved out of
// RendererPanel so the creation panes get it too; see ViewportRegionSelect.cpp.
#include "Core/dspch.hpp"

#include "Presentation/Panels/ViewportSelection.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <limits>
#include <optional>
#include <vector>

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <imgui.h>
#include <ImGuizmo.h>

#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Core/Logging/Logger.hpp"
#include "Events/RendererEvents.hpp"
#include "Renderer/RendererLayer.hpp"
#include "Renderer/RendererViewCamera.hpp"
#include "Renderer/RendererWindowState.hpp"
#include "Renderer/Scene/SceneComponents.hpp"
#include "Renderer/Scene/SceneSystem.hpp"
#include "Renderer/Scene/SelectionHitTest.hpp"

namespace DefectStudio
{
	// Gizmo for the current label selection - any mix of pinned measurements (PinnedMeasurement::
	// worldOffset/rotationOffsetRadians/style.scale) and free labels (FreeLabel::worldPosition/
	// rotationRadians/style.scale) - sibling of RenderTransformGizmo above, same screen-space
	// pick/drag philosophy as the atom gizmo (ImGuizmo's own picking is unreliable here too, see that
	// function's big comment). Pivot is the live centroid of every selected item, same "recomputed
	// every frame" convention the atom gizmo's pivot uses. Translate draws the familiar
	// shaft+arrowhead 3-axis handles and moves every selected item's own position field by the same
	// world-space delta (a rigid group move). Rotate/Scale use a single ring-drag around the pivot
	// instead - no per-axis handles, since a camera-facing billboard has only one meaningful rotation
	// axis (its own normal) and one meaningful scale (uniform glyph size) - and apply their delta to
	// each selected item's OWN rotation/scale field in place (spin/grow each independently, not
	// orbit their positions around the shared centroid the way a rigid-body atom rotate/scale would -
	// a label's rotation only ever means "this text's own orientation", never a position transform).
	// Pushes one undo snapshot at drag start via PushPinnedMeasurementUndoSnapshot (Ctrl+Alt+U/
	// Ctrl+Alt+Shift+U - see RendererEvents::Viewport::UndoLabelsRequested), covering both kinds
	// together (LabelUndoSnapshot) regardless of which are in this drag.
	bool RenderLabelTransformGizmo(
		RendererWindowState &windowState, const ImVec2 &imageOrigin, const ImVec2 &imageSize, bool hovered)
	{
		if (windowState.camera == nullptr)
		{
			windowState.labelGizmoDragging = false;
			windowState.labelGizmoModalDrag = false;
			windowState.labelGizmoAxis = -1;
			return false;
		}

		if (windowState.selectedPinnedMeasurements.empty() && windowState.selectedFreeLabels.empty())
		{
			windowState.labelGizmoDragging = false;
			windowState.labelGizmoModalDrag = false;
			windowState.labelGizmoAxis = -1;
			return false;
		}

		// A pin's live position is its label entity's TransformComponent (anchor + worldOffset, kept
		// current by SceneSystem::UpdateLabelTransforms, called once per frame before this) rather than
		// resolving the anchor itself - a free label has no such entity, so its position is just its
		// own worldPosition directly, always live, nothing to keep in sync.
		auto resolvePosition = [&](bool isPin, std::size_t index, glm::vec3 &outPosition) -> bool {
			if (isPin)
			{
				if (index >= windowState.pinnedMeasurements.size())
					return false;
				Entity labelEntity = windowState.sceneRegistry.LabelEntityAt(index);
				if (!labelEntity || !labelEntity.HasComponent<TransformComponent>())
					return false;
				outPosition = labelEntity.GetComponent<TransformComponent>().position;
				return true;
			}
			if (index >= windowState.freeLabels.size())
				return false;
			outPosition = windowState.freeLabels[index].worldPosition;
			return true;
		};
		// The mutable fields a drag actually writes to - PinnedMeasurement and FreeLabel are
		// different struct types so there's no single "the object" pointer to return, just the three
		// fields both happen to have.
		auto resolveMutableFields = [&](bool isPin, std::size_t index, glm::vec3 *&outPosition,
			float *&outRotation, float *&outScale) -> bool {
			if (isPin)
			{
				if (index >= windowState.pinnedMeasurements.size())
					return false;
				RendererWindowState::PinnedMeasurement &pin = windowState.pinnedMeasurements[index];
				outPosition = &pin.worldOffset;
				outRotation = &pin.rotationOffsetRadians;
				outScale = &pin.style.scale;
				return true;
			}
			if (index >= windowState.freeLabels.size())
				return false;
			RendererWindowState::FreeLabel &label = windowState.freeLabels[index];
			outPosition = &label.worldPosition;
			outRotation = &label.rotationRadians;
			outScale = &label.style.scale;
			return true;
		};

		glm::vec3 pivot(0.0f);
		int pivotCount = 0;
		for (const std::size_t pinIndex : windowState.selectedPinnedMeasurements)
		{
			glm::vec3 position(0.0f);
			if (resolvePosition(true, pinIndex, position))
			{
				pivot += position;
				++pivotCount;
			}
		}
		for (const std::size_t labelIndex : windowState.selectedFreeLabels)
		{
			glm::vec3 position(0.0f);
			if (resolvePosition(false, labelIndex, position))
			{
				pivot += position;
				++pivotCount;
			}
		}
		if (pivotCount == 0)
			return false;
		pivot /= static_cast<float>(pivotCount);

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

		glm::vec2 pivotScreen(0.0f);
		const bool pivotOnScreen = projectToScreen(pivot, pivotScreen);
		const glm::vec2 mousePos(ImGui::GetMousePos().x, ImGui::GetMousePos().y);

		// Smaller pick band than the atom gizmo's kPickMaxDistance (130px) - a label has no atom
		// underneath competing for the same screen space, so there is no need for as much clearance.
		// (Was 14/70 - bumped up, the gizmo was cramped enough to overlap the label's own text.)
		constexpr float kPickMinDistance = 20.0f;
		constexpr float kPickMaxDistance = 100.0f;

		// Snapshots every selected item's current position/rotation/scale into
		// windowState.labelGizmoDragTargets (for cancel-revert and Scale's ratio-from-start math) and
		// pushes the one undo entry for the whole drag - called once, right as a ring or axis drag
		// starts.
		auto beginDrag = [&]() {
			windowState.labelGizmoDragTargets.clear();
			auto captureTarget = [&](bool isPin, std::size_t index) {
				glm::vec3 *positionPtr = nullptr;
				float *rotationPtr = nullptr;
				float *scalePtr = nullptr;
				if (!resolveMutableFields(isPin, index, positionPtr, rotationPtr, scalePtr))
					return;
				windowState.labelGizmoDragTargets.push_back(
					RendererWindowState::LabelGizmoDragTarget{isPin, index, *positionPtr, *rotationPtr, *scalePtr});
			};
			for (const std::size_t pinIndex : windowState.selectedPinnedMeasurements)
				captureTarget(true, pinIndex);
			for (const std::size_t labelIndex : windowState.selectedFreeLabels)
				captureTarget(false, labelIndex);
			PushPinnedMeasurementUndoSnapshot(windowState);
		};

		if (windowState.gizmoOperation == GizmoOperation::Rotate || windowState.gizmoOperation == GizmoOperation::Scale)
		{
			const bool isRotate = windowState.gizmoOperation == GizmoOperation::Rotate;
			constexpr ImU32 kRingColor = IM_COL32(235, 235, 235, 200);
			constexpr ImU32 kRingActiveColor = IM_COL32(255, 200, 60, 220);

			if (pivotOnScreen && !windowState.labelGizmoDragging)
				ImGui::GetWindowDrawList()->AddCircle(
					ImVec2(pivotScreen.x, pivotScreen.y), kPickMaxDistance, kRingColor, 48, 2.0f);

			const float radialNow = pivotOnScreen ? glm::length(mousePos - pivotScreen) : -1.0f;
			const bool hoveringRing = hovered && pivotOnScreen && !windowState.labelGizmoDragging &&
				radialNow >= kPickMinDistance && radialNow <= kPickMaxDistance;

			if (hoveringRing && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
			{
				beginDrag();
				windowState.labelGizmoDragging = true;
				windowState.labelGizmoModalDrag = false; // no keyboard-modal path for Rotate/Scale
				windowState.labelGizmoAxis = -2; // sentinel: ring drag, no X/Y/Z handle
				windowState.labelGizmoLastMousePos = mousePos;
				windowState.labelGizmoDragStartRadial = std::max(kPickMinDistance, radialNow);
			}

			if (windowState.labelGizmoDragging && windowState.labelGizmoAxis == -2)
			{
				if (pivotOnScreen)
					ImGui::GetWindowDrawList()->AddCircle(
						ImVec2(pivotScreen.x, pivotScreen.y), kPickMaxDistance, kRingActiveColor, 48, 3.0f);

				// Cancel: Escape or right-click reverts every target to its pre-drag snapshot, same
				// convention as the atom gizmo's fallback drag.
				if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) || ImGui::IsMouseClicked(ImGuiMouseButton_Right))
				{
					for (const RendererWindowState::LabelGizmoDragTarget &target : windowState.labelGizmoDragTargets)
					{
						glm::vec3 *positionPtr = nullptr;
						float *rotationPtr = nullptr;
						float *scalePtr = nullptr;
						if (!resolveMutableFields(target.isPin, target.index, positionPtr, rotationPtr, scalePtr))
							continue;
						*rotationPtr = target.startRotation;
						*scalePtr = target.startScale;
					}
					windowState.labelGizmoDragging = false;
					windowState.labelGizmoAxis = -1;
					return true;
				}

				if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
				{
					if (isRotate)
					{
						// atan2's y negated: screen space is y-down, while the billboard's own local
						// "up" (matched to cameraUp in labels.vert) is y-up - without this the drag
						// would feel mirrored (drag clockwise on screen, label spins the other way).
						const glm::vec2 fromPivotLast = windowState.labelGizmoLastMousePos - pivotScreen;
						const glm::vec2 fromPivotNow = mousePos - pivotScreen;
						if (glm::length(fromPivotLast) > 1.0f && glm::length(fromPivotNow) > 1.0f)
						{
							const float lastAngle = std::atan2(-fromPivotLast.y, fromPivotLast.x);
							const float nowAngle = std::atan2(-fromPivotNow.y, fromPivotNow.x);
							float deltaAngle = nowAngle - lastAngle;
							while (deltaAngle > glm::pi<float>())
								deltaAngle -= glm::two_pi<float>();
							while (deltaAngle < -glm::pi<float>())
								deltaAngle += glm::two_pi<float>();
							// Incremental (each selected item spins by the same per-frame delta), unlike
							// Scale below which recomputes from the frozen start value every frame.
							for (const RendererWindowState::LabelGizmoDragTarget &target :
								windowState.labelGizmoDragTargets)
							{
								glm::vec3 *positionPtr = nullptr;
								float *rotationPtr = nullptr;
								float *scalePtr = nullptr;
								if (resolveMutableFields(target.isPin, target.index, positionPtr, rotationPtr, scalePtr))
									*rotationPtr += deltaAngle;
							}
						}
					}
					else
					{
						// Blender S-style: scale ratio is the current radial distance from the pivot
						// over the distance at drag start, not a per-frame delta - dragging back to the
						// start radius always returns exactly to each target's own start scale.
						const float currentRadial = std::max(1.0f, glm::length(mousePos - pivotScreen));
						const float ratio = currentRadial / windowState.labelGizmoDragStartRadial;
						for (const RendererWindowState::LabelGizmoDragTarget &target :
							windowState.labelGizmoDragTargets)
						{
							glm::vec3 *positionPtr = nullptr;
							float *rotationPtr = nullptr;
							float *scalePtr = nullptr;
							if (resolveMutableFields(target.isPin, target.index, positionPtr, rotationPtr, scalePtr))
								*scalePtr = glm::clamp(target.startScale * ratio, 0.1f, 8.0f);
						}
					}
					windowState.labelGizmoLastMousePos = mousePos;
					return true;
				}

				windowState.labelGizmoDragging = false;
				windowState.labelGizmoAxis = -1;
				return true;
			}

			return hoveringRing;
		}

		// Translate (default / GizmoOperation::Translate) - 3-axis arrow handles, same shape as the
		// atom gizmo's own translate branch.
		constexpr glm::vec3 kWorldAxes[3] = {
			glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f)};
		constexpr ImU32 kAxisLockColors[3] = {
			IM_COL32(230, 70, 70, 200), IM_COL32(90, 210, 90, 200), IM_COL32(90, 150, 240, 200)};

		glm::vec2 axisScreenDir[3];
		float axisPixelsPerWorld[3] = {1.0f, 1.0f, 1.0f};
		bool axisValid[3] = {false, false, false};
		if (pivotOnScreen)
		{
			for (int axis = 0; axis < 3; ++axis)
			{
				glm::vec2 probeScreen;
				if (!projectToScreen(pivot + kWorldAxes[axis], probeScreen))
					continue;
				const glm::vec2 axisVec = probeScreen - pivotScreen;
				const float axisPixels = glm::length(axisVec);
				if (axisPixels < 1.0f)
					continue;
				axisScreenDir[axis] = axisVec / axisPixels;
				axisPixelsPerWorld[axis] = axisPixels;
				axisValid[axis] = true;
			}
		}

		if (pivotOnScreen && !windowState.labelGizmoDragging)
		{
			ImDrawList *axisDrawList = ImGui::GetWindowDrawList();
			constexpr float kArrowHeadLength = 16.0f;
			constexpr float kArrowHeadHalfWidth = 7.0f;
			for (int axis = 0; axis < 3; ++axis)
			{
				if (!axisValid[axis])
					continue;
				const glm::vec2 dir = axisScreenDir[axis];
				const glm::vec2 perp(-dir.y, dir.x);
				const glm::vec2 tip = glm::vec2(pivotScreen.x, pivotScreen.y) + dir * kPickMaxDistance;
				const glm::vec2 headBase = tip - dir * kArrowHeadLength;
				axisDrawList->AddLine(
					ImVec2(pivotScreen.x, pivotScreen.y), ImVec2(headBase.x, headBase.y), kAxisLockColors[axis], 2.5f);
				const glm::vec2 headLeft = headBase + perp * kArrowHeadHalfWidth;
				const glm::vec2 headRight = headBase - perp * kArrowHeadHalfWidth;
				axisDrawList->AddTriangleFilled(
					ImVec2(tip.x, tip.y), ImVec2(headLeft.x, headLeft.y), ImVec2(headRight.x, headRight.y),
					kAxisLockColors[axis]);
			}
		}

		int hoveredAxis = -1;
		if (hovered && pivotOnScreen && !windowState.labelGizmoDragging)
		{
			constexpr float kPickPerpTolerance = 16.0f;
			const glm::vec2 fromPivot = mousePos - pivotScreen;
			const float radial = glm::length(fromPivot);
			float bestPerp = kPickPerpTolerance;
			if (radial >= kPickMinDistance && radial <= kPickMaxDistance)
			{
				for (int axis = 0; axis < 3; ++axis)
				{
					if (!axisValid[axis])
						continue;
					const float along = glm::dot(fromPivot, axisScreenDir[axis]);
					if (along <= 0.0f)
						continue;
					const float perp = glm::length(fromPivot - axisScreenDir[axis] * along);
					if (perp < bestPerp)
					{
						bestPerp = perp;
						hoveredAxis = axis;
					}
				}
			}

			if (hoveredAxis >= 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
			{
				beginDrag();
				windowState.labelGizmoDragging = true;
				windowState.labelGizmoModalDrag = false;
				windowState.labelGizmoAxis = hoveredAxis;
				windowState.labelGizmoLastMousePos = mousePos;
				windowState.labelGizmoDragAxisScreenDir = axisScreenDir[hoveredAxis];
				windowState.labelGizmoDragAxisWorldDir = kWorldAxes[hoveredAxis];
				windowState.labelGizmoDragPixelsPerWorld = std::max(1.0f, axisPixelsPerWorld[hoveredAxis]);
			}
		}

		// Blender-style modal axis-locked translate: pressing X/Y/Z with no mouse button held starts a
		// drag constrained to that world axis, following the mouse without needing to click the handle
		// first - same modal convention as the atom gizmo's fallbackModalDrag (confirm with a
		// left-click, cancel with Escape/right-click, handled below).
		if (hovered && pivotOnScreen && !windowState.labelGizmoDragging)
		{
			constexpr ImGuiKey kModalAxisKeys[3] = {ImGuiKey_X, ImGuiKey_Y, ImGuiKey_Z};
			for (int axis = 0; axis < 3; ++axis)
			{
				if (!axisValid[axis] || !ImGui::IsKeyPressed(kModalAxisKeys[axis], false))
					continue;
				beginDrag();
				windowState.labelGizmoDragging = true;
				windowState.labelGizmoModalDrag = true;
				windowState.labelGizmoAxis = axis;
				windowState.labelGizmoLastMousePos = mousePos;
				windowState.labelGizmoDragAxisScreenDir = axisScreenDir[axis];
				windowState.labelGizmoDragAxisWorldDir = kWorldAxes[axis];
				windowState.labelGizmoDragPixelsPerWorld = std::max(1.0f, axisPixelsPerWorld[axis]);
				break;
			}
		}

		if (windowState.labelGizmoDragging && windowState.labelGizmoAxis >= 0)
		{
			if (pivotOnScreen)
			{
				ImDrawList *drawList = ImGui::GetWindowDrawList();
				const glm::vec2 dir = windowState.labelGizmoDragAxisScreenDir;
				const ImVec2 farA(pivotScreen.x - dir.x * 10000.0f, pivotScreen.y - dir.y * 10000.0f);
				const ImVec2 farB(pivotScreen.x + dir.x * 10000.0f, pivotScreen.y + dir.y * 10000.0f);
				drawList->AddLine(farA, farB, kAxisLockColors[windowState.labelGizmoAxis], 2.5f);
			}

			// Cancel: Escape or right-click reverts every target to its pre-drag snapshot, same
			// convention as the atom gizmo's fallback drag.
			if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) || ImGui::IsMouseClicked(ImGuiMouseButton_Right))
			{
				for (const RendererWindowState::LabelGizmoDragTarget &target : windowState.labelGizmoDragTargets)
				{
					glm::vec3 *positionPtr = nullptr;
					float *rotationPtr = nullptr;
					float *scalePtr = nullptr;
					if (resolveMutableFields(target.isPin, target.index, positionPtr, rotationPtr, scalePtr))
						*positionPtr = target.startPosition;
				}
				windowState.labelGizmoDragging = false;
				windowState.labelGizmoModalDrag = false;
				windowState.labelGizmoAxis = -1;
				return true;
			}

			// Modal drag (started via X/Y/Z, see above) confirms on a left-click instead of on
			// release - the live value below is already applied, so confirming is just stopping.
			if (windowState.labelGizmoModalDrag && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
			{
				windowState.labelGizmoDragging = false;
				windowState.labelGizmoModalDrag = false;
				windowState.labelGizmoAxis = -1;
				return true;
			}

			if (windowState.labelGizmoModalDrag || ImGui::IsMouseDown(ImGuiMouseButton_Left))
			{
				const glm::vec2 delta = mousePos - windowState.labelGizmoLastMousePos;
				windowState.labelGizmoLastMousePos = mousePos;
				const float deltaOnAxisPixels = glm::dot(delta, windowState.labelGizmoDragAxisScreenDir);
				const float deltaOnAxisWorld = deltaOnAxisPixels / windowState.labelGizmoDragPixelsPerWorld;
				const glm::vec3 worldDelta = windowState.labelGizmoDragAxisWorldDir * deltaOnAxisWorld;
				// Rigid group move - every selected item's own position field shifts by the same
				// world-space delta, incrementally each frame (matches the single-select code this
				// replaced; see the Rotate/Scale branch above for why rotation/scale instead apply
				// per-item in place rather than moving anyone's position).
				for (const RendererWindowState::LabelGizmoDragTarget &target : windowState.labelGizmoDragTargets)
				{
					glm::vec3 *positionPtr = nullptr;
					float *rotationPtr = nullptr;
					float *scalePtr = nullptr;
					if (resolveMutableFields(target.isPin, target.index, positionPtr, rotationPtr, scalePtr))
						*positionPtr += worldDelta;
				}
				return true;
			}

			// Button released (non-modal only - a modal drag never reaches here, it only stops via
			// confirm/cancel above) - commit (the drag already mutated positions live, nothing further
			// to apply) and consume this one release frame same as the atom gizmo does.
			windowState.labelGizmoDragging = false;
			windowState.labelGizmoAxis = -1;
			return true;
		}

		return hoveredAxis >= 0;
	}
} // namespace DefectStudio
