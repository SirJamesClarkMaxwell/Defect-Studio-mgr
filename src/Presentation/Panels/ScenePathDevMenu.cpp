#include "Core/dspch.hpp"

#include "Presentation/Panels/ScenePathDevMenu.hpp"

#include <imgui.h>

#include "Renderer/RendererWindowState.hpp"
#include "Renderer/Path/PathCommands.hpp"
#include "Renderer/RendererLayer.hpp"

namespace DefectStudio
{
	ScenePath MakeDevScenePath(const ScenePathDevPreset preset, const glm::vec3 &worldPosition)
	{
		ScenePath path;
		// No persistKey here: EnsureScenePersistKeys mints a unique one once the path is in a window,
		// and a shared literal made every dev path the same saved object - MergeWindowSceneObjects
		// keys on persistKey, so four of them collapsed into one on save.
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

		// The same sink every other scene-object edit already uses; without it AddScenePath applies
		// the edit and records no history, which is exactly what a dev menu must not do.
		PathEditContext context;
		context.window = &windowState;
		context.pushUndo = [](RendererWindowState &window, SceneObjectsSnapshot before, const std::string &) {
			PushSceneObjectsUndoSnapshot(window, std::move(before));
		};

		const auto add = [&](const ScenePathDevPreset preset, const char *label) {
			if (!ImGui::MenuItem(label))
				return;
			(void)AddScenePath(context, MakeDevScenePath(preset, worldPosition));
		};
		add(ScenePathDevPreset::Line, "Line");
		add(ScenePathDevPreset::Cubic, "Cubic");
		add(ScenePathDevPreset::Arc, "Arc");
		ImGui::EndMenu();
	}
} // namespace DefectStudio
