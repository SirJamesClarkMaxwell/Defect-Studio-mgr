// Transform gizmo for the SceneArrow selection. Moved out of RendererPanel; see
// ViewportRegionSelect.cpp. Over the ~500-line file rule because it is one function.
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
	// Drawn translate/rotate/scale gizmo for the current SceneArrow selection - sibling of
	// RenderLabelTransformGizmo above (own state, no ICommand/UndoStack, PushPinnedMeasurementUndoSnapshot
	// on drag start). Translate differs from every other gizmo in this file: exactly one arrow selected
	// draws THREE pick points (Start, End, and the midpoint for a rigid whole-arrow move) instead of one,
	// since an arrow (unlike an atom or a label) is defined by two independent positions. More than one
	// arrow selected collapses to a single group-centroid pivot (every selected arrow's both endpoints
	// move together), matching the existing raw-drag system's own "multi-selection is always rigid" rule
	// (see SceneArrowDragTarget above). Modal X/Y/Z axis-lock-start (no mouse button, follows the mouse
	// immediately) only applies to the midpoint/group pivot - Start/End are click-and-drag only, a
	// deliberate scope trim (the common "nudge one endpoint" case is already well served by a direct
	// click-drag; modal start earns its keep on the whole-arrow move, the more frequent action). Rotate is
	// a free trackball (grab anywhere in the ring band, drag freely - same cross(camera-forward,
	// drag-direction) math as the atom gizmo's own trackball path, no axis-lock modal) around either the
	// selection's own centroid or windowState.cursor3DPosition, per sceneArrowGizmoPivotMode (the toolbar
	// toggle). Scale has no pivot concept at all - it only multiplies shaftWidth/headWidth/headLength in
	// place (per the user's explicit "thickness only, never touches start/end" decision).
	bool RenderSceneArrowTransformGizmo(
		RendererWindowState &windowState, const ImVec2 &imageOrigin, const ImVec2 &imageSize, bool hovered)
	{
		if (windowState.selectedSceneArrows.empty() || windowState.camera == nullptr)
		{
			windowState.sceneArrowGizmoDragging = false;
			windowState.sceneArrowGizmoModalDrag = false;
			windowState.sceneArrowGizmoAxis = -1;
			return false;
		}

		glm::vec3 centroid(0.0f);
		int centroidCount = 0;
		for (const std::size_t index : windowState.selectedSceneArrows)
		{
			if (index >= windowState.sceneArrows.size())
				continue;
			centroid += windowState.sceneArrows[index].start;
			centroid += windowState.sceneArrows[index].end;
			centroidCount += 2;
		}
		if (centroidCount == 0)
			return false;
		centroid /= static_cast<float>(centroidCount);

		const bool isSingleArrow = windowState.selectedSceneArrows.size() == 1 &&
			windowState.selectedSceneArrows.front() < windowState.sceneArrows.size();

		using DragTarget = RendererWindowState::SceneArrowDragTarget;
		constexpr glm::vec3 kWorldAxes[3] = {
			glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f)};
		constexpr ImU32 kAxisLockColors[3] = {
			IM_COL32(230, 70, 70, 200), IM_COL32(90, 210, 90, 200), IM_COL32(90, 150, 240, 200)};

		// Which single Start/End/midpoint point currently owns the drawn axis-triad (item 3 of this
		// round's feedback: showing all 3 at once on a single arrow was cluttered/ambiguous) - resets
		// to the whole-arrow midpoint whenever the selection changes to a different arrow (or stops
		// being a single-arrow selection), so switching arrows never leaves a stale sub-target active.
		const std::size_t currentSingleArrowIndex =
			isSingleArrow ? windowState.selectedSceneArrows.front() : static_cast<std::size_t>(-1);
		if (windowState.sceneArrowGizmoActiveArrowIndex != currentSingleArrowIndex)
		{
			windowState.sceneArrowGizmoActiveArrowIndex = currentSingleArrowIndex;
			windowState.sceneArrowGizmoActiveTarget = DragTarget::Both;
		}

		const glm::mat4 view = windowState.camera->ViewMatrix();
		const glm::mat4 viewProjection = windowState.camera->ProjectionMatrix() * view;
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
		const glm::vec2 mousePos(ImGui::GetMousePos().x, ImGui::GetMousePos().y);

		// Snapshots every selected arrow's start/end/shaftWidth/headWidth/headLength (for cancel-revert
		// and Scale's ratio-from-start math) and pushes the one undo entry for the whole drag - called
		// once, right as a handle/ring drag starts.
		auto beginDrag = [&]() {
			windowState.sceneArrowGizmoDragTargets.clear();
			for (const std::size_t index : windowState.selectedSceneArrows)
			{
				if (index >= windowState.sceneArrows.size())
					continue;
				const RendererWindowState::SceneArrow &arrow = windowState.sceneArrows[index];
				RendererWindowState::SceneArrowGizmoDragTarget target;
				target.index = index;
				target.startPosition = arrow.start;
				target.endPosition = arrow.end;
				target.startShaftWidth = arrow.style.shaftWidth;
				target.startHeadWidth = arrow.style.headWidth;
				target.startHeadLength = arrow.style.headLength;
				windowState.sceneArrowGizmoDragTargets.push_back(target);
			}
			PushPinnedMeasurementUndoSnapshot(windowState);
		};

		// Shared by both rotate-cancel paths below (free trackball and axis-locked) - restores every
		// dragged arrow's pre-drag start/end/thickness from the beginDrag() snapshot.
		auto revertRotateOrScale = [&]() {
			for (const RendererWindowState::SceneArrowGizmoDragTarget &target : windowState.sceneArrowGizmoDragTargets)
			{
				if (target.index >= windowState.sceneArrows.size())
					continue;
				RendererWindowState::SceneArrow &arrow = windowState.sceneArrows[target.index];
				arrow.start = target.startPosition;
				arrow.end = target.endPosition;
				arrow.style.shaftWidth = target.startShaftWidth;
				arrow.style.headWidth = target.startHeadWidth;
				arrow.style.headLength = target.startHeadLength;
			}
		};

		if (windowState.gizmoOperation == GizmoOperation::Rotate || windowState.gizmoOperation == GizmoOperation::Scale)
		{
			const bool isRotate = windowState.gizmoOperation == GizmoOperation::Rotate;
			const glm::vec3 pivot = (isRotate &&
										 windowState.sceneArrowGizmoPivotMode == RendererWindowState::ArrowGizmoPivotMode::Cursor3D &&
										 windowState.cursor3DPlaced)
				? windowState.cursor3DPosition
				: centroid;

			glm::vec2 pivotScreen(0.0f);
			const bool pivotOnScreen = projectToScreen(pivot, pivotScreen);

			constexpr float kPickMinDistance = 20.0f;
			constexpr float kPickMaxDistance = 100.0f;
			constexpr ImU32 kRingColor = IM_COL32(235, 235, 235, 200);
			constexpr ImU32 kRingActiveColor = IM_COL32(255, 200, 60, 220);

			if (pivotOnScreen && !windowState.sceneArrowGizmoDragging)
				ImGui::GetWindowDrawList()->AddCircle(
					ImVec2(pivotScreen.x, pivotScreen.y), kPickMaxDistance, kRingColor, 48, 2.0f);

			const float radialNow = pivotOnScreen ? glm::length(mousePos - pivotScreen) : -1.0f;
			const bool hoveringRing = hovered && pivotOnScreen && !windowState.sceneArrowGizmoDragging &&
				radialNow >= kPickMinDistance && radialNow <= kPickMaxDistance;

			if (hoveringRing && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
			{
				beginDrag();
				windowState.sceneArrowGizmoDragging = true;
				windowState.sceneArrowGizmoModalDrag = false;
				windowState.sceneArrowGizmoAxis = -2; // sentinel: ring drag, no X/Y/Z handle
				windowState.sceneArrowGizmoLastMousePos = mousePos;
				windowState.sceneArrowGizmoDragStartRadial = std::max(kPickMinDistance, radialNow);
			}

			// Blender-style modal axis-locked rotate (item 4 of this round's feedback: the free trackball
			// had no way to constrain which axis the arrow spun around) - pressing X/Y/Z with no mouse
			// button down starts a rotation locked to that world axis, following the mouse's angular
			// motion around the pivot; same modal convention as the atom gizmo's rotate (see
			// RenderTransformGizmo's ROTATE branch, ported here) - confirm with left-click, cancel with
			// Escape/right-click, re-press a different axis key mid-drag to switch it.
			if (isRotate && hovered && pivotOnScreen && !windowState.sceneArrowGizmoDragging)
			{
				constexpr ImGuiKey kModalAxisKeys[3] = {ImGuiKey_X, ImGuiKey_Y, ImGuiKey_Z};
				for (int axis = 0; axis < 3; ++axis)
				{
					if (!ImGui::IsKeyPressed(kModalAxisKeys[axis], false))
						continue;
					beginDrag();
					windowState.sceneArrowGizmoDragging = true;
					windowState.sceneArrowGizmoModalDrag = true;
					windowState.sceneArrowGizmoAxis = axis;
					windowState.sceneArrowGizmoLastMousePos = mousePos;
					break;
				}
			}

			if (windowState.sceneArrowGizmoDragging && windowState.sceneArrowGizmoAxis >= 0 && windowState.sceneArrowGizmoAxis <= 2)
			{
				if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) || ImGui::IsMouseClicked(ImGuiMouseButton_Right))
				{
					revertRotateOrScale();
					windowState.sceneArrowGizmoDragging = false;
					windowState.sceneArrowGizmoModalDrag = false;
					windowState.sceneArrowGizmoAxis = -1;
					return true;
				}

				constexpr ImGuiKey kRotateAxisKeys[3] = {ImGuiKey_X, ImGuiKey_Y, ImGuiKey_Z};
				for (int axis = 0; axis < 3; ++axis)
				{
					if (axis != windowState.sceneArrowGizmoAxis && ImGui::IsKeyPressed(kRotateAxisKeys[axis], false))
						windowState.sceneArrowGizmoAxis = axis;
				}

				const bool confirmed = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
				if (pivotOnScreen)
				{
					ImGui::GetWindowDrawList()->AddCircle(
						ImVec2(pivotScreen.x, pivotScreen.y), kPickMaxDistance, kAxisLockColors[windowState.sceneArrowGizmoAxis],
						48, 3.0f);

					const glm::vec3 cameraForward = -glm::vec3(view[0][2], view[1][2], view[2][2]);
					const glm::vec3 lockedAxisWorld = kWorldAxes[windowState.sceneArrowGizmoAxis];
					// Screen Y is flipped vs standard math convention, and a right-hand rotation around
					// an axis pointing away from the viewer reads as clockwise on-screen - both flips
					// cancel out when the axis points toward the viewer instead (same derivation as the
					// atom gizmo's locked rotate).
					const float rotationSign = glm::dot(lockedAxisWorld, cameraForward) >= 0.0f ? -1.0f : 1.0f;

					const glm::vec2 fromPivotLast = windowState.sceneArrowGizmoLastMousePos - pivotScreen;
					const glm::vec2 fromPivotNow = mousePos - pivotScreen;
					if (glm::length(fromPivotLast) > 1.0f && glm::length(fromPivotNow) > 1.0f)
					{
						const float lastAngle = std::atan2(fromPivotLast.y, fromPivotLast.x);
						const float nowAngle = std::atan2(fromPivotNow.y, fromPivotNow.x);
						float deltaAngle = nowAngle - lastAngle;
						while (deltaAngle > glm::pi<float>())
							deltaAngle -= glm::two_pi<float>();
						while (deltaAngle < -glm::pi<float>())
							deltaAngle += glm::two_pi<float>();

						const glm::quat rotation = glm::angleAxis(deltaAngle * rotationSign, lockedAxisWorld);
						for (const RendererWindowState::SceneArrowGizmoDragTarget &target : windowState.sceneArrowGizmoDragTargets)
						{
							if (target.index >= windowState.sceneArrows.size())
								continue;
							RendererWindowState::SceneArrow &arrow = windowState.sceneArrows[target.index];
							arrow.start = pivot + rotation * (arrow.start - pivot);
							arrow.end = pivot + rotation * (arrow.end - pivot);
						}
					}
					windowState.sceneArrowGizmoLastMousePos = mousePos;
				}

				if (confirmed)
				{
					windowState.sceneArrowGizmoDragging = false;
					windowState.sceneArrowGizmoModalDrag = false;
					windowState.sceneArrowGizmoAxis = -1;
					return true;
				}
				return true;
			}

			if (windowState.sceneArrowGizmoDragging && windowState.sceneArrowGizmoAxis == -2)
			{
				if (pivotOnScreen)
					ImGui::GetWindowDrawList()->AddCircle(
						ImVec2(pivotScreen.x, pivotScreen.y), kPickMaxDistance, kRingActiveColor, 48, 3.0f);

				if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) || ImGui::IsMouseClicked(ImGuiMouseButton_Right))
				{
					revertRotateOrScale();
					windowState.sceneArrowGizmoDragging = false;
					windowState.sceneArrowGizmoAxis = -1;
					return true;
				}

				if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
				{
					if (isRotate)
					{
						const glm::vec3 cameraRight(view[0][0], view[1][0], view[2][0]);
						const glm::vec3 cameraUp(view[0][1], view[1][1], view[2][1]);
						const glm::vec3 cameraForward = -glm::vec3(view[0][2], view[1][2], view[2][2]);

						const glm::vec2 delta = mousePos - windowState.sceneArrowGizmoLastMousePos;
						windowState.sceneArrowGizmoLastMousePos = mousePos;
						const glm::vec3 dragWorldDir = cameraRight * delta.x - cameraUp * delta.y;
						const float dragLength = glm::length(dragWorldDir);
						if (dragLength > 0.0001f)
						{
							const glm::vec3 rotationAxis = glm::normalize(glm::cross(cameraForward, dragWorldDir));
							constexpr float kRadiansPerPixel = 0.006f;
							const glm::quat rotation = glm::angleAxis(glm::length(delta) * kRadiansPerPixel, rotationAxis);
							for (const RendererWindowState::SceneArrowGizmoDragTarget &target :
								windowState.sceneArrowGizmoDragTargets)
							{
								if (target.index >= windowState.sceneArrows.size())
									continue;
								RendererWindowState::SceneArrow &arrow = windowState.sceneArrows[target.index];
								arrow.start = pivot + rotation * (arrow.start - pivot);
								arrow.end = pivot + rotation * (arrow.end - pivot);
							}
						}
					}
					else
					{
						// Blender S-style: scale ratio is the current radial distance from the pivot over
						// the distance at drag start, not a per-frame delta - dragging back to the start
						// radius always returns exactly to each target's own start thickness.
						const float currentRadial = std::max(1.0f, glm::length(mousePos - pivotScreen));
						const float ratio = currentRadial / windowState.sceneArrowGizmoDragStartRadial;
						for (const RendererWindowState::SceneArrowGizmoDragTarget &target :
							windowState.sceneArrowGizmoDragTargets)
						{
							if (target.index >= windowState.sceneArrows.size())
								continue;
							RendererWindowState::ArrowStyle &style = windowState.sceneArrows[target.index].style;
							style.shaftWidth = std::clamp(target.startShaftWidth * ratio, 0.005f, 1.0f);
							style.headWidth = std::clamp(target.startHeadWidth * ratio, 0.01f, 1.0f);
							style.headLength = std::clamp(target.startHeadLength * ratio, 0.01f, 2.0f);
						}
					}
					windowState.sceneArrowGizmoLastMousePos = mousePos;
					return true;
				}

				windowState.sceneArrowGizmoDragging = false;
				windowState.sceneArrowGizmoAxis = -1;
				return true;
			}

			return hoveringRing;
		}

		// Translate. kWorldAxes/kAxisLockColors/DragTarget are declared earlier in this function (the
		// Rotate branch's axis-lock needs them too).

		// Applies the axis-drag's per-frame world delta to whichever field(s) sceneArrowGizmoEndpointTarget
		// says are active - shared by both the click-drag and modal-drag continuation below.
		auto applyTranslateDelta = [&](const glm::vec3 &worldDelta) {
			if (windowState.sceneArrowGizmoEndpointTarget == DragTarget::Both)
			{
				for (const RendererWindowState::SceneArrowGizmoDragTarget &target : windowState.sceneArrowGizmoDragTargets)
				{
					if (target.index >= windowState.sceneArrows.size())
						continue;
					windowState.sceneArrows[target.index].start += worldDelta;
					windowState.sceneArrows[target.index].end += worldDelta;
				}
				return;
			}
			if (windowState.sceneArrowGizmoDragTargets.empty())
				return;
			const std::size_t index = windowState.sceneArrowGizmoDragTargets.front().index;
			if (index >= windowState.sceneArrows.size())
				return;
			if (windowState.sceneArrowGizmoEndpointTarget == DragTarget::Start)
				windowState.sceneArrows[index].start += worldDelta;
			else
				windowState.sceneArrows[index].end += worldDelta;
		};
		auto revertTranslate = [&]() {
			for (const RendererWindowState::SceneArrowGizmoDragTarget &target : windowState.sceneArrowGizmoDragTargets)
			{
				if (target.index >= windowState.sceneArrows.size())
					continue;
				windowState.sceneArrows[target.index].start = target.startPosition;
				windowState.sceneArrows[target.index].end = target.endPosition;
			}
		};

		if (windowState.sceneArrowGizmoDragging)
		{
			glm::vec3 activePivotWorld = centroid;
			if (windowState.sceneArrowGizmoEndpointTarget != DragTarget::Both && !windowState.sceneArrowGizmoDragTargets.empty())
			{
				const std::size_t index = windowState.sceneArrowGizmoDragTargets.front().index;
				if (index < windowState.sceneArrows.size())
					activePivotWorld = windowState.sceneArrowGizmoEndpointTarget == DragTarget::Start
						? windowState.sceneArrows[index].start
						: windowState.sceneArrows[index].end;
			}
			glm::vec2 activePivotScreen(0.0f);
			if (projectToScreen(activePivotWorld, activePivotScreen))
			{
				ImDrawList *drawList = ImGui::GetWindowDrawList();
				const glm::vec2 dir = windowState.sceneArrowGizmoDragAxisScreenDir;
				const ImVec2 farA(activePivotScreen.x - dir.x * 10000.0f, activePivotScreen.y - dir.y * 10000.0f);
				const ImVec2 farB(activePivotScreen.x + dir.x * 10000.0f, activePivotScreen.y + dir.y * 10000.0f);
				drawList->AddLine(farA, farB, kAxisLockColors[windowState.sceneArrowGizmoAxis], 2.5f);
			}

			if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) || ImGui::IsMouseClicked(ImGuiMouseButton_Right))
			{
				revertTranslate();
				windowState.sceneArrowGizmoDragging = false;
				windowState.sceneArrowGizmoModalDrag = false;
				windowState.sceneArrowGizmoAxis = -1;
				return true;
			}

			if (windowState.sceneArrowGizmoModalDrag && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
			{
				windowState.sceneArrowGizmoDragging = false;
				windowState.sceneArrowGizmoModalDrag = false;
				windowState.sceneArrowGizmoAxis = -1;
				return true;
			}

			if (windowState.sceneArrowGizmoModalDrag || ImGui::IsMouseDown(ImGuiMouseButton_Left))
			{
				const glm::vec2 delta = mousePos - windowState.sceneArrowGizmoLastMousePos;
				windowState.sceneArrowGizmoLastMousePos = mousePos;
				const float deltaOnAxisPixels = glm::dot(delta, windowState.sceneArrowGizmoDragAxisScreenDir);
				const float deltaOnAxisWorld = deltaOnAxisPixels / windowState.sceneArrowGizmoDragPixelsPerWorld;
				applyTranslateDelta(windowState.sceneArrowGizmoDragAxisWorldDir * deltaOnAxisWorld);
				return true;
			}

			windowState.sceneArrowGizmoDragging = false;
			windowState.sceneArrowGizmoAxis = -1;
			return true;
		}

		// Not dragging - draw + hit-test each candidate pivot (Start/End/midpoint for a single selected
		// arrow, or just the group centroid for a multi-selection).
		struct Candidate
		{
			glm::vec3 world;
			DragTarget target;
			float pickMaxDistance;
			bool allowModal;
		};
		std::vector<Candidate> candidates;
		if (isSingleArrow)
		{
			const RendererWindowState::SceneArrow &arrow = windowState.sceneArrows[windowState.selectedSceneArrows.front()];
			candidates.push_back({arrow.start, DragTarget::Start, 60.0f, false});
			candidates.push_back({arrow.end, DragTarget::End, 60.0f, false});
			candidates.push_back({(arrow.start + arrow.end) * 0.5f, DragTarget::Both, 100.0f, true});
		}
		else
		{
			candidates.push_back({centroid, DragTarget::Both, 100.0f, true});
		}

		constexpr float kPickMinDistance = 20.0f;
		bool anyCandidateHovered = false;
		for (const Candidate &candidate : candidates)
		{
			glm::vec2 pivotScreen(0.0f);
			if (!projectToScreen(candidate.world, pivotScreen))
				continue;

			// Item 3 of this round's feedback: showing Start/End/midpoint's full axis-triad all at once
			// on a single selected arrow was cluttered and ambiguous about which drag would move what.
			// Only the active target (sceneArrowGizmoActiveTarget, reset to Both/midpoint on selection
			// change above) gets drawn/hit-tested as a real gizmo below - the other candidates on a
			// single-arrow selection are just plain click-to-activate dots.
			if (isSingleArrow && candidate.target != windowState.sceneArrowGizmoActiveTarget)
			{
				ImGui::GetWindowDrawList()->AddCircleFilled(
					ImVec2(pivotScreen.x, pivotScreen.y), 5.0f, IM_COL32(190, 190, 190, 190));
				if (hovered)
				{
					constexpr float kInactivePickRadius = 10.0f;
					if (glm::length(mousePos - pivotScreen) <= kInactivePickRadius)
					{
						anyCandidateHovered = true;
						if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
							windowState.sceneArrowGizmoActiveTarget = candidate.target;
					}
				}
				continue;
			}

			glm::vec2 axisScreenDir[3];
			float axisPixelsPerWorld[3] = {1.0f, 1.0f, 1.0f};
			bool axisValid[3] = {false, false, false};
			for (int axis = 0; axis < 3; ++axis)
			{
				glm::vec2 probeScreen;
				if (!projectToScreen(candidate.world + kWorldAxes[axis], probeScreen))
					continue;
				const glm::vec2 axisVec = probeScreen - pivotScreen;
				const float axisPixels = glm::length(axisVec);
				if (axisPixels < 1.0f)
					continue;
				axisScreenDir[axis] = axisVec / axisPixels;
				axisPixelsPerWorld[axis] = axisPixels;
				axisValid[axis] = true;
			}

			ImDrawList *axisDrawList = ImGui::GetWindowDrawList();
			constexpr float kArrowHeadLength = 14.0f;
			constexpr float kArrowHeadHalfWidth = 6.0f;
			for (int axis = 0; axis < 3; ++axis)
			{
				if (!axisValid[axis])
					continue;
				const glm::vec2 dir = axisScreenDir[axis];
				const glm::vec2 perp(-dir.y, dir.x);
				const glm::vec2 tip = glm::vec2(pivotScreen.x, pivotScreen.y) + dir * candidate.pickMaxDistance;
				const glm::vec2 headBase = tip - dir * kArrowHeadLength;
				axisDrawList->AddLine(
					ImVec2(pivotScreen.x, pivotScreen.y), ImVec2(headBase.x, headBase.y), kAxisLockColors[axis], 2.5f);
				const glm::vec2 headLeft = headBase + perp * kArrowHeadHalfWidth;
				const glm::vec2 headRight = headBase - perp * kArrowHeadHalfWidth;
				axisDrawList->AddTriangleFilled(
					ImVec2(tip.x, tip.y), ImVec2(headLeft.x, headLeft.y), ImVec2(headRight.x, headRight.y), kAxisLockColors[axis]);
			}
			axisDrawList->AddCircleFilled(ImVec2(pivotScreen.x, pivotScreen.y), 4.0f, IM_COL32(235, 235, 235, 255));

			int hoveredAxis = -1;
			if (hovered)
			{
				constexpr float kPickPerpTolerance = 14.0f;
				const glm::vec2 fromPivot = mousePos - pivotScreen;
				const float radial = glm::length(fromPivot);
				float bestPerp = kPickPerpTolerance;
				if (radial >= kPickMinDistance && radial <= candidate.pickMaxDistance)
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
			}

			if (hoveredAxis >= 0)
			{
				anyCandidateHovered = true;
				if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
				{
					beginDrag();
					windowState.sceneArrowGizmoDragging = true;
					windowState.sceneArrowGizmoModalDrag = false;
					windowState.sceneArrowGizmoAxis = hoveredAxis;
					windowState.sceneArrowGizmoEndpointTarget = candidate.target;
					windowState.sceneArrowGizmoLastMousePos = mousePos;
					windowState.sceneArrowGizmoDragAxisScreenDir = axisScreenDir[hoveredAxis];
					windowState.sceneArrowGizmoDragAxisWorldDir = kWorldAxes[hoveredAxis];
					windowState.sceneArrowGizmoDragPixelsPerWorld = std::max(1.0f, axisPixelsPerWorld[hoveredAxis]);
					return true;
				}
			}

			if (candidate.allowModal && hovered)
			{
				constexpr ImGuiKey kModalAxisKeys[3] = {ImGuiKey_X, ImGuiKey_Y, ImGuiKey_Z};
				for (int axis = 0; axis < 3; ++axis)
				{
					if (!axisValid[axis] || !ImGui::IsKeyPressed(kModalAxisKeys[axis], false))
						continue;
					beginDrag();
					windowState.sceneArrowGizmoDragging = true;
					windowState.sceneArrowGizmoModalDrag = true;
					windowState.sceneArrowGizmoAxis = axis;
					windowState.sceneArrowGizmoEndpointTarget = candidate.target;
					windowState.sceneArrowGizmoLastMousePos = mousePos;
					windowState.sceneArrowGizmoDragAxisScreenDir = axisScreenDir[axis];
					windowState.sceneArrowGizmoDragAxisWorldDir = kWorldAxes[axis];
					windowState.sceneArrowGizmoDragPixelsPerWorld = std::max(1.0f, axisPixelsPerWorld[axis]);
					return true;
				}
			}
		}

		return anyCandidateHovered;
	}
} // namespace DefectStudio
