#include "Core/dspch.hpp"

#include "Presentation/Panels/ViewportPathInsert.hpp"

#include <algorithm>
#include <cmath>
#include <imgui.h>

#include "Core/Logging/Logger.hpp"
#include "Presentation/Panels/ScenePathOperations.hpp"
#include "Renderer/Path/PathBindingResolver.hpp"
#include "Renderer/Path/PathEvaluator.hpp"
#include "Renderer/Path/PathPicking.hpp"
#include "Renderer/Path/PathTessellator.hpp"
#include "Renderer/RendererViewCamera.hpp"
#include "Renderer/RendererWindowState.hpp"
#include "Renderer/Scene/SceneSystem.hpp"
#include "Renderer/Scene/SelectionHitTest.hpp"

namespace DefectStudio
{
	bool HandleViewportPathInsert(RendererWindowState &window, const ImVec2 &origin,
		const ImVec2 &size, const bool hovered)
	{
		auto &session = window.pathEdit;
		if (!session.InsertRequested() && !session.InsertPreview()) return false;
		if (!session.IsActive() || !window.paths || !window.camera || size.x <= 0 || size.y <= 0 ||
			window.modalTransform.has_value())
		{
			session.CancelInsert();
			return false;
		}
		const auto path = window.paths->Store().Find(session.Path());
		if (!path || !path->visible || !path->renderable)
		{
			session.CancelInsert();
			return false;
		}
		const auto resolved = ResolveNodePositions(*path, SceneSystem::MakePathBindingContext(window));
		const auto view = window.camera->ViewMatrix();
		const auto viewProjection = window.camera->ProjectionMatrix() * view;
		if (session.InsertRequested())
		{
			if (!hovered) { session.CancelInsert(); return false; }
			PathPickSettings settings;
			settings.viewProjection = viewProjection;
			settings.viewportSize = {size.x, size.y};
			const auto mouse = ImGui::GetMousePos();
			settings.cursor = {mouse.x - origin.x, mouse.y - origin.y};
			settings.cameraRight = {view[0][0], view[1][0], view[2][0]};
			settings.editMode = true;
			settings.segmentOnly = true;
			const auto evaluated = Tessellate(*path, resolved, TessellationSettings{});
			const auto hit = PickPath(*path, resolved, evaluated, settings);
			if (hit.kind != PathPickKind::Segment) { session.CancelInsert(); return false; }
			session.BeginInsert(hit.element);
		}
		const auto found = std::find_if(path->segments.begin(), path->segments.end(), [&](const PathSegment &segment) {
			return segment.id == session.InsertPreview()->segment;
		});
		if (found == path->segments.end()) { session.CancelInsert(); return true; }
		const auto index = static_cast<std::size_t>(found - path->segments.begin());
		auto &io = ImGui::GetIO();
		if (hovered && io.MouseWheel != 0)
		{
			session.ChangeInsertCount(io.MouseWheel > 0 ? static_cast<int>(std::ceil(io.MouseWheel))
				: static_cast<int>(std::floor(io.MouseWheel)));
			io.MouseWheel = 0;
		}
		if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) || ImGui::IsMouseClicked(ImGuiMouseButton_Right))
		{
			session.CancelInsert();
			return true;
		}
		const auto count = session.InsertPreview()->count;
		auto &draw = *ImGui::GetWindowDrawList();
		draw.PushClipRect(origin, ImVec2(origin.x + size.x, origin.y + size.y), true);
		for (std::size_t node = 1; node <= count; ++node)
		{
			const auto sample = EvaluateSegment(*path, resolved, index, static_cast<double>(node) / (count + 1));
			if (!sample) continue;
			const auto screen = SelectionHitTest::ProjectToScreen(viewProjection, {size.x, size.y}, glm::vec3(sample->position));
			if (screen) draw.AddCircleFilled(ImVec2(origin.x + screen->x, origin.y + screen->y), 5.0f, IM_COL32(255, 170, 40, 255));
		}
		draw.PopClipRect();
		if ((hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) || ImGui::IsKeyPressed(ImGuiKey_Enter, false))
		{
			const auto result = InsertScenePathNodes(MakeWindowPathEditContext(window), path->id, index, count);
			if (!result) DS_LOG_WARN("Path insert failed: {}", result.Error().technicalDetails);
			else
			{
				session.SetElementMode(PathElementMode::NodeHandle);
				session.SetSelection(*result);
			}
			session.CancelInsert();
		}
		return true;
	}
}
