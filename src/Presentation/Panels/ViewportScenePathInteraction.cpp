// Click-select for ScenePaths. Sits between the orbital and the plane handler in the chain: a
// path is a thin object that a plane is usually drawn behind, so it gets first refusal on the
// click over the plane, and last refusal against everything smaller than it.
#include "Core/dspch.hpp"

#include "Presentation/Panels/ViewportSelection.hpp"

#include <algorithm>
#include <optional>
#include <vector>

#include <imgui.h>

#include "Core/Logging/Logger.hpp"
#include "Presentation/Panels/ScenePathOperations.hpp"
#include "Presentation/Panels/SceneObjectMultiSelection.hpp"
#include "Renderer/Path/PathBindingResolver.hpp"
#include "Renderer/Path/PathHandleGeometry.hpp"
#include "Renderer/Path/PathPicking.hpp"
#include "Renderer/Path/ScenePathPicking.hpp"
#include "Renderer/RendererViewCamera.hpp"
#include "Renderer/RendererWindowState.hpp"

namespace DefectStudio
{
	namespace
	{
		template <typename T>
		void ReportPathEditResult(const Result<T> &result)
		{
			if (!result)
				DS_LOG_WARN("Path edit failed: {}", result.Error().technicalDetails);
		}

		[[nodiscard]] PathPickSettings BuildPathPickSettings(
			const RendererWindowState &windowState, const ImVec2 &imageOrigin, const ImVec2 &imageSize,
			const bool editMode)
		{
			const glm::mat4 view = windowState.camera->ViewMatrix();
			PathPickSettings settings;
			settings.viewProjection = windowState.camera->ProjectionMatrix() * view;
			settings.viewportSize = glm::vec2(imageSize.x, imageSize.y);
			const ImVec2 mouse = ImGui::GetMousePos();
			settings.cursor = glm::vec2(mouse.x - imageOrigin.x, mouse.y - imageOrigin.y);
			settings.cameraRight = glm::vec3(view[0][0], view[1][0], view[2][0]);
			settings.activeElement = windowState.pathEdit.ActiveElement();
			settings.editMode = editMode;
			return settings;
		}

		[[nodiscard]] PathPickResult PickEditedPath(
			const RendererWindowState &windowState, const ScenePath &path, const PathPickSettings &settings)
		{
			const CachedPathGeometry *cached = windowState.paths->Caches().FindLastBuilt(path.id);
			static const EvaluatedPath empty;
			return PickPath(
				path, ResolveNodePositions(path, BindingContext{}), cached == nullptr ? empty : cached->evaluated, settings);
		}
	} // namespace

	// Plain click replaces the selection; Ctrl/Shift-click toggles in Edit Mode. Object Mode keeps the
	// same replace/toggle rules as arrows, orbitals and planes, while Edit Mode routes the click to the
	// one path opened by the session.
	bool HandleScenePathInteraction(
		RendererWindowState &windowState, const ImVec2 &imageOrigin, const ImVec2 &imageSize, bool hovered)
	{
		const bool focused = windowState.lastFocusedState;
		if (focused && ImGui::IsKeyPressed(ImGuiKey_Tab, false))
		{
			if (windowState.pathEdit.IsActive())
				windowState.pathEdit.Leave();
			else if (windowState.selectedScenePaths.size() == 1 && windowState.paths != nullptr &&
				windowState.paths->Store().Find(windowState.selectedScenePaths.front()) != nullptr)
				windowState.pathEdit.Enter(windowState.selectedScenePaths.front());
			else
				return false;
			return true;
		}

		if (windowState.pathEdit.IsActive())
		{
			if (focused && ImGui::IsKeyPressed(ImGuiKey_Escape, false))
			{
				windowState.pathEdit.Leave();
				return true;
			}
			if (focused && ImGui::IsKeyPressed(ImGuiKey_1, false))
			{
				windowState.pathEdit.SetElementMode(PathElementMode::NodeHandle);
				return true;
			}
			if (focused && ImGui::IsKeyPressed(ImGuiKey_2, false))
			{
				windowState.pathEdit.SetElementMode(PathElementMode::Segment);
				return true;
			}
			if (focused && ImGui::IsKeyPressed(ImGuiKey_3, false))
			{
				windowState.pathEdit.SetElementMode(PathElementMode::WholePath);
				return true;
			}

			bool keyboardAction = false;
			const ImGuiIO &shortcutIo = ImGui::GetIO();
			const bool noModifiers = !shortcutIo.KeyCtrl && !shortcutIo.KeyAlt &&
				!shortcutIo.KeyShift && !shortcutIo.KeySuper;
			if (focused && noModifiers && ImGui::IsKeyPressed(ImGuiKey_E, false))
			{
				ReportPathEditResult(ExtendSelectedScenePathEnd(windowState));
				keyboardAction = true;
			}
			if (focused && noModifiers && ImGui::IsKeyPressed(ImGuiKey_Delete, false))
			{
				ReportPathEditResult(DeleteSelectedScenePathNodes(windowState));
				keyboardAction = true;
			}
			if (focused && noModifiers && ImGui::IsKeyPressed(ImGuiKey_V, false))
			{
				ImGui::OpenPopup("##PathHandleType");
				keyboardAction = true;
			}
			if (ImGui::BeginPopup("##PathHandleType"))
			{
				for (const auto &[label, type] : {
					std::pair{"Free", BezierHandleType::Free},
					std::pair{"Aligned", BezierHandleType::Aligned},
					std::pair{"Vector", BezierHandleType::Vector},
					std::pair{"Auto", BezierHandleType::Auto}})
					if (ImGui::MenuItem(label))
						ReportPathEditResult(SetSelectedScenePathHandleType(windowState, type));
				ImGui::EndPopup();
			}
			if (keyboardAction)
				return true;

			if (!hovered || !ImGui::IsMouseClicked(ImGuiMouseButton_Left) || windowState.camera == nullptr ||
				windowState.paths == nullptr || imageSize.x <= 0.0f || imageSize.y <= 0.0f)
				return false;

			const ImVec2 mouse = ImGui::GetMousePos();
			const float relativeX = mouse.x - imageOrigin.x;
			const float relativeY = mouse.y - imageOrigin.y;
			if (relativeX < 0.0f || relativeY < 0.0f || relativeX > imageSize.x || relativeY > imageSize.y)
				return false;

			const ScenePath *path = windowState.paths->Store().Find(windowState.pathEdit.Path());
			if (path != nullptr)
			{
				PathPickSettings settings = BuildPathPickSettings(windowState, imageOrigin, imageSize, true);
				settings.cursor = glm::vec2(relativeX, relativeY);
				const PathPickResult hit = PickEditedPath(windowState, *path, settings);
				if (hit.Hit())
				{
					const ImGuiIO &io = ImGui::GetIO();
					const SceneOutlinerSelectionModifier modifier = (io.KeyCtrl || io.KeyShift)
						? SceneOutlinerSelectionModifier::Toggle
						: SceneOutlinerSelectionModifier::Replace;
					// Range needs an ordered row list and an anchor; a viewport click has neither, so
					// both are empty and only Replace and Toggle can come out of this.
					const std::vector<PathElementId> orderedRows;
					windowState.pathEdit.SetSelection(ApplySceneOutlinerSelection(
						windowState.pathEdit.Selection(),
						orderedRows,
						hit.element,
						std::optional<PathElementId>{},
						modifier));
				}
				else
					windowState.pathEdit.ClearSelection();
			}
			else
				windowState.pathEdit.ClearSelection();
			// A miss is still consumed: empty space clears the element selection but never leaves Edit Mode
			// or falls through to Object Mode's whole-path picker.
			return true;
		}

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

		PathPickSettings settings = BuildPathPickSettings(windowState, imageOrigin, imageSize, false);
		settings.cursor = glm::vec2(relativeX, relativeY);
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

	bool IsScenePathMarkerUnderMouse(
		const RendererWindowState &windowState, const ImVec2 &imageOrigin, const ImVec2 &imageSize)
	{
		if (!windowState.pathEdit.IsActive() || windowState.camera == nullptr || windowState.paths == nullptr ||
			imageSize.x <= 0.0f || imageSize.y <= 0.0f)
			return false;

		const ScenePath *path = windowState.paths->Store().Find(windowState.pathEdit.Path());
		if (path == nullptr || !path->visible || !path->renderable)
			return false;

		const glm::vec2 mouse = glm::vec2(ImGui::GetMousePos().x, ImGui::GetMousePos().y) -
			glm::vec2(imageOrigin.x, imageOrigin.y);
		const std::vector<PathHandleMarker> markers = BuildPathHandleMarkers(
			*path,
			ResolveNodePositions(*path, BindingContext{}),
			windowState.camera->ProjectionMatrix() * windowState.camera->ViewMatrix(),
			glm::vec2(imageSize.x, imageSize.y),
			windowState.pathEdit.ActiveElement());
		return std::any_of(markers.begin(), markers.end(), [&mouse](const PathHandleMarker &marker) {
			return glm::distance(mouse, marker.screenPosition) <= marker.pickRadius;
		});
	}
} // namespace DefectStudio
