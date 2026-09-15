// Click-select and endpoint drag for SceneArrows. Moved out of RendererPanel; see
// ViewportRegionSelect.cpp.
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
	[[nodiscard]] static std::size_t ArrowIndex(const RendererWindowState &windowState, const SceneObjectId id)
	{
		const auto found = std::find_if(windowState.sceneArrows.begin(), windowState.sceneArrows.end(), [id](const auto &arrow) { return arrow.id == id; });
		return found == windowState.sceneArrows.end() ? windowState.sceneArrows.size() : static_cast<std::size_t>(std::distance(windowState.sceneArrows.begin(), found));
	}

	// Click-select + drag for sceneArrows - same click/Ctrl-toggle/drag shape as
	// HandleFreeLabelInteraction above, but the hit-test is against a SEGMENT (start->end), not a
	// single anchor point, and a single selected arrow's drag moves only whichever endpoint was
	// actually grabbed (screen-space proximity at click time decides that, no drawn gizmo widget
	// needed - same idea as IsBondUnderScreenPosition's proximity band, just resolved once instead
	// of every frame). Multiple selected arrows always move rigidly together (every selected arrow's
	// start AND end shift by the same delta), same group-drag convention as labels.
	bool HandleSceneArrowInteraction(
		RendererWindowState &windowState, const ImVec2 &imageOrigin, const ImVec2 &imageSize, bool hovered)
	{
		if (!windowState.pickLabels || windowState.camera == nullptr)
			return false;

		using SceneArrow = RendererWindowState::SceneArrow;
		using DragTarget = RendererWindowState::SceneArrowDragTarget;
		using ArrowKind = RendererWindowState::ArrowKind;

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

		if (windowState.sceneArrowDragging)
		{
			if (!ImGui::IsMouseDown(ImGuiMouseButton_Left) || windowState.selectedSceneArrows.empty())
			{
				windowState.sceneArrowDragging = false;
				return false;
			}

			const std::size_t referenceIndex = ArrowIndex(windowState, windowState.selectedSceneArrows.back());
			if (referenceIndex >= windowState.sceneArrows.size())
			{
				windowState.sceneArrowDragging = false;
				return false;
			}
			const bool singleSelection = windowState.selectedSceneArrows.size() == 1;
			const SceneArrow &referenceArrow = windowState.sceneArrows[referenceIndex];
			glm::vec3 referencePosition = (referenceArrow.start + referenceArrow.end) * 0.5f;
			if (singleSelection && windowState.sceneArrowDragTarget == DragTarget::Start)
				referencePosition = referenceArrow.start;
			else if (singleSelection && windowState.sceneArrowDragTarget == DragTarget::End)
				referencePosition = referenceArrow.end;

			glm::vec2 anchorScreen, rightProbe, upProbe;
			if (projectToScreen(referencePosition, anchorScreen) &&
				projectToScreen(referencePosition + cameraRight, rightProbe) &&
				projectToScreen(referencePosition + cameraUp, upProbe))
			{
				const float pixelsPerWorldRight = std::max(1.0f, glm::length(rightProbe - anchorScreen));
				const float pixelsPerWorldUp = std::max(1.0f, glm::length(upProbe - anchorScreen));
				const glm::vec2 mousePos(ImGui::GetMousePos().x, ImGui::GetMousePos().y);
				const glm::vec2 deltaPixels = mousePos - windowState.sceneArrowDragLastMouse;
				const glm::vec3 worldDelta = cameraRight * (deltaPixels.x / pixelsPerWorldRight) -
					cameraUp * (deltaPixels.y / pixelsPerWorldUp);
				for (const SceneObjectId arrowId : windowState.selectedSceneArrows)
				{
					const std::size_t arrowIndex = ArrowIndex(windowState, arrowId);
					if (arrowIndex >= windowState.sceneArrows.size())
						continue;
					SceneArrow &arrow = windowState.sceneArrows[arrowIndex];
					if (singleSelection && windowState.sceneArrowDragTarget == DragTarget::Start)
						arrow.start += worldDelta;
					else if (singleSelection && windowState.sceneArrowDragTarget == DragTarget::End)
						arrow.end += worldDelta;
					else
					{
						arrow.start += worldDelta;
						arrow.end += worldDelta;
					}
				}
				windowState.sceneArrowDragLastMouse = mousePos;
			}
			return true;
		}

		if (!hovered || !ImGui::IsMouseClicked(ImGuiMouseButton_Left))
			return false;

		const bool additive = ImGui::GetIO().KeyCtrl;
		const glm::vec2 mousePos(ImGui::GetMousePos().x, ImGui::GetMousePos().y);
		// Tolerance scales with each arrow's actual rendered width instead of a flat radius (docs/
		// scene_arrow_rework_plan_corrected.md Step 9) - a thick arrow should be easier to click than
		// a thin one, and the shape actually drawn (triangle head) should be what gets picked. Arrow2D's
		// shaftWidth/headWidth/headLength/outlineWidth are already screen-space pixels (Section 8), so
		// they're used as-is; Arrow3D/Line are world-space and need a per-arrow world->pixel probe -
		// same camera-right projection trick the drag code above uses for pixelsPerWorldRight/Up.
		int hitIndex = -1;
		float bestDistance = std::numeric_limits<float>::max();
		glm::vec2 hitScreenStart(0.0f), hitScreenEnd(0.0f);
		float hitShaftHalfPx = 0.0f;
		for (std::size_t i = 0; i < windowState.sceneArrows.size(); ++i)
		{
			const SceneArrow &candidate = windowState.sceneArrows[i];
			glm::vec2 screenStart, screenEnd;
			if (!projectToScreen(candidate.start, screenStart) || !projectToScreen(candidate.end, screenEnd))
				continue;

			const RendererWindowState::ArrowStyle &style = candidate.style;
			const bool isArrow2D = candidate.kind == ArrowKind::Arrow2D;
			float pixelsPerWorld = 1.0f;
			if (!isArrow2D)
			{
				const glm::vec3 midWorld = (candidate.start + candidate.end) * 0.5f;
				glm::vec2 screenMid, rightProbe;
				if (projectToScreen(midWorld, screenMid) && projectToScreen(midWorld + cameraRight, rightProbe))
					pixelsPerWorld = std::max(glm::length(rightProbe - screenMid), 0.0001f);
			}
			const float shaftHalfPx = isArrow2D ? style.shaftWidth * 0.5f : style.shaftWidth * 0.5f * pixelsPerWorld;
			const float outlinePx = isArrow2D ? style.outlineWidth : 0.0f;
			const float shaftTolerance = std::max(12.0f, shaftHalfPx + outlinePx + 4.0f);
			const float shaftDistance = SelectionHitTest::DistancePointToSegment(mousePos, screenStart, screenEnd);
			float bestForCandidate =
				shaftDistance <= shaftTolerance ? shaftDistance : std::numeric_limits<float>::max();

			const glm::vec2 screenAxis = screenEnd - screenStart;
			const float screenLength = glm::length(screenAxis);
			const glm::vec2 dirScreen = screenLength > 0.0001f ? screenAxis / screenLength : glm::vec2(1.0f, 0.0f);
			if (isArrow2D)
			{
				// Mirrors renderSceneArrows' Arrow2D head triangle exactly (same clamp too), so the
				// pickable area matches the visible shape instead of a generic radius around the tip.
				const glm::vec2 perpScreen(-dirScreen.y, dirScreen.x);
				const float headLengthPx = std::min(style.headLength, 0.45f * screenLength);
				const glm::vec2 headBase = screenEnd - dirScreen * headLengthPx;
				const float headDistance = SelectionHitTest::DistancePointToTriangle2D(
					mousePos, headBase + perpScreen * (style.headWidth * 0.5f),
					headBase - perpScreen * (style.headWidth * 0.5f), screenEnd);
				if (headDistance <= 4.0f)
					bestForCandidate = std::min(bestForCandidate, headDistance);
			}
			else if (candidate.kind == ArrowKind::Arrow3D)
			{
				const float headRadiusPx = style.headWidth * 0.5f * pixelsPerWorld;
				const float headTolerance = std::max(14.0f, headRadiusPx);
				const float headDistance = glm::length(mousePos - screenEnd);
				if (headDistance <= headTolerance)
					bestForCandidate = std::min(bestForCandidate, headDistance);
			}
			// Line: no head test (doc Step 9).

			if (bestForCandidate < bestDistance)
			{
				bestDistance = bestForCandidate;
				hitIndex = static_cast<int>(i);
				hitScreenStart = screenStart;
				hitScreenEnd = screenEnd;
				hitShaftHalfPx = shaftHalfPx;
			}
		}

		std::vector<SceneObjectId> &selection = windowState.selectedSceneArrows;
		if (hitIndex < 0)
		{
			if (!additive)
				selection.clear();
			return false;
		}

		const SceneObjectId hitArrow = windowState.sceneArrows[static_cast<std::size_t>(hitIndex)].id;
		const auto existing = std::find(selection.begin(), selection.end(), hitArrow);
		// Mutual exclusivity with label selection, same convention as pin/free-label clicks above.
		windowState.selectedPinnedMeasurements.clear();
		windowState.selectedFreeLabels.clear();

		if (additive)
		{
			if (existing != selection.end())
				selection.erase(existing);
			else
				selection.push_back(hitArrow);
			return true;
		}

		if (existing == selection.end())
		{
			selection.clear();
			selection.push_back(hitArrow);
		}

		// Which endpoint this click actually grabbed - only matters once the selection is (or
		// becomes) exactly this one arrow; a multi-selection drag always moves every selected
		// arrow's start AND end together regardless of this. Scales with the hit arrow's own shaft
		// half-width, same reasoning as the shaft/head tolerances above (doc Step 9).
		const float endpointTolerance = std::max(14.0f, hitShaftHalfPx + 8.0f);
		const float distanceToStart = glm::length(mousePos - hitScreenStart);
		const float distanceToEnd = glm::length(mousePos - hitScreenEnd);
		if (distanceToStart <= endpointTolerance && distanceToStart <= distanceToEnd)
			windowState.sceneArrowDragTarget = DragTarget::Start;
		else if (distanceToEnd <= endpointTolerance)
			windowState.sceneArrowDragTarget = DragTarget::End;
		else
			windowState.sceneArrowDragTarget = DragTarget::Both;
		windowState.sceneArrowGizmoActiveArrowIndex = static_cast<std::size_t>(hitIndex);
		windowState.sceneArrowGizmoActiveTarget = windowState.sceneArrowDragTarget;

		PushPinnedMeasurementUndoSnapshot(windowState);
		windowState.sceneArrowDragging = true;
		windowState.sceneArrowDragLastMouse = mousePos;
		return true;
	}
} // namespace DefectStudio
