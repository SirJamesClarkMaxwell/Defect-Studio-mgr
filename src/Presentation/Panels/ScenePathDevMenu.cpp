#include "Core/dspch.hpp"

#include "Presentation/Panels/ScenePathDevMenu.hpp"

#include <imgui.h>

#include "Renderer/RendererWindowState.hpp"
#include "Renderer/Scene/SceneSystem.hpp"

namespace DefectStudio
{
	ScenePath MakeDevScenePath(const ScenePathDevPreset preset, const glm::vec3 &worldPosition)
	{
		ScenePath path;
		path.persistKey = "dev-path";
		path.name = "Dev path";
		path.visible = true;
		path.renderable = true;
		path.style.profile = StrokeProfile::Round;
		path.style.width = 0.05f;
		path.style.color = glm::vec3(0.95f, 0.35f, 0.1f);
		path.style.endDecoration.kind = PathDecorationKind::Arrow;

		PathNode start;
		start.id = AllocateElementId(path);
		start.position = worldPosition + glm::vec3(-1.0f, 0.0f, 0.0f);
		PathNode end;
		end.id = AllocateElementId(path);
		end.position = worldPosition + glm::vec3(1.0f, 0.0f, 0.0f);
		path.nodes = {start, end};

		PathSegment segment;
		segment.id = AllocateElementId(path);
		switch (preset)
		{
		case ScenePathDevPreset::Line:
			segment.data = LineSegmentData{};
			break;
		case ScenePathDevPreset::Cubic:
		{
			CubicBezierSegmentData cubic;
			cubic.startHandle.id = AllocateElementId(path);
			cubic.startHandle.position = worldPosition + glm::vec3(-0.5f, 1.25f, 0.0f);
			cubic.startHandle.type = BezierHandleType::Free;
			cubic.endHandle.id = AllocateElementId(path);
			cubic.endHandle.position = worldPosition + glm::vec3(0.5f, 1.25f, 0.0f);
			cubic.endHandle.type = BezierHandleType::Free;
			segment.data = cubic;
			break;
		}
		case ScenePathDevPreset::Arc:
		{
			CircularArcSegmentData arc;
			arc.planeNormal = glm::vec3(0.0f, 0.0f, 1.0f);
			arc.signedSweepRadians = glm::half_pi<float>();
			segment.data = arc;
			break;
		}
		}
		path.segments.push_back(std::move(segment));
		return path;
	}

	void DrawScenePathDevAddMenu(RendererWindowState &windowState, const glm::vec3 &worldPosition)
	{
		if (!ImGui::BeginMenu("Path (dev)"))
			return;

		const auto add = [&](const ScenePathDevPreset preset, const char *label) {
			if (!ImGui::MenuItem(label))
				return;
			SceneSystem::AppendScenePath(windowState, MakeDevScenePath(preset, worldPosition));
			SceneSystem::SyncLabelEntities(windowState.sceneRegistry, windowState);
		};
		add(ScenePathDevPreset::Line, "Line");
		add(ScenePathDevPreset::Cubic, "Cubic");
		add(ScenePathDevPreset::Arc, "Arc");
		ImGui::EndMenu();
	}
} // namespace DefectStudio
