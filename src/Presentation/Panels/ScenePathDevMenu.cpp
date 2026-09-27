#include "Core/dspch.hpp"

#include "Presentation/Panels/ScenePathDevMenu.hpp"

#include <imgui.h>

#include <array>
#include <utility>

#include "Renderer/RendererWindowState.hpp"
#include "Renderer/Path/PathTopology.hpp"
#include "Presentation/Panels/ScenePathOperations.hpp"
#include "Renderer/RendererLayer.hpp"

namespace DefectStudio
{
	namespace
	{
		void AddDecorationGalleryImpl(RendererWindowState &windowState, const glm::vec3 &worldPosition)
		{
			constexpr std::array<std::pair<PathDecorationKind, const char *>, 8> decorations = {{
				{PathDecorationKind::Arrow, "Arrow"},
				{PathDecorationKind::Stealth, "Stealth"},
				{PathDecorationKind::Latex, "Latex"},
				{PathDecorationKind::Bar, "Bar"},
				{PathDecorationKind::Circle, "Circle"},
				{PathDecorationKind::Square, "Square"},
				{PathDecorationKind::Diamond, "Diamond"},
				{PathDecorationKind::Kite, "Kite"},
			}};
			constexpr float rowSpacing = 0.5f;

			// Match the multi-path operations: one snapshot, then the normal AddScenePath validation
			// and insertion path with its per-item undo sink disabled.
			PushSceneObjectsUndoSnapshot(windowState, CaptureSceneObjectsSnapshot(windowState));
			const PathEditContext context = MakeSilentPathEditContext(windowState);
			for (std::size_t index = 0; index < decorations.size(); ++index)
			{
				const auto [kind, name] = decorations[index];
				const float row = static_cast<float>(index) - (static_cast<float>(decorations.size()) - 1.0f) * 0.5f;
				ScenePath path = MakeDevScenePath(
					ScenePathDevPreset::Line,
					worldPosition + glm::vec3(0.0f, 0.0f, row * rowSpacing),
					StrokeProfile::Round);
				path.name = name;
				path.style.startDecoration.kind = kind;
				path.style.endDecoration.kind = PathDecorationKind::Arrow;
				(void)AddScenePath(context, std::move(path));
			}
		}
	} // namespace

	ScenePath MakeDevScenePath(const ScenePathDevPreset preset, const glm::vec3 &worldPosition, const StrokeProfile profile)
	{
		ScenePath path;
		// No persistKey here: EnsureScenePersistKeys mints a unique one once the path is in a window,
		// and a shared literal made every dev path the same saved object - MergeWindowSceneObjects
		// keys on persistKey, so four of them collapsed into one on save.
		path.name = "Dev path";
		path.visible = true;
		path.renderable = true;
		path.style.profile = profile;
		// The default orbit view uses +X right and +Z up while looking along +Y; +Z makes the X-axis
		// Flat ribbon lie in that screen plane instead of edge-on.
		if (profile == StrokeProfile::Flat)
			path.style.ribbonNormal = glm::vec3(0.0f, 0.0f, 1.0f);
		path.style.width = profile == StrokeProfile::Round ? 0.05f : 0.08f;
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
			cubic.startHandle.offset = glm::vec3(0.5f, 1.25f, 0.0f);
			cubic.startHandle.type = BezierHandleType::Free;
			cubic.endHandle.id = AllocateElementId(path);
			cubic.endHandle.offset = glm::vec3(-0.5f, 1.25f, 0.0f);
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
		MovePathOriginToCentre(path);
		return path;
	}

	namespace
	{
		PathSegment MakeDevCubicSegment(ScenePath &path, const glm::vec3 &startOffset, const glm::vec3 &endOffset)
		{
			PathSegment segment;
			segment.id = AllocateElementId(path);
			CubicBezierSegmentData cubic;
			cubic.startHandle.id = AllocateElementId(path);
			cubic.startHandle.offset = startOffset;
			cubic.startHandle.type = BezierHandleType::Free;
			cubic.endHandle.id = AllocateElementId(path);
			cubic.endHandle.offset = endOffset;
			cubic.endHandle.type = BezierHandleType::Free;
			segment.data = std::move(cubic);
			return segment;
		}

		ScenePath MakeDevCompositePath(
			const glm::vec3 &worldPosition, const StrokeProfile profile, const bool mixedSegments)
		{
			// Reuse the normal dev-path defaults and origin setup; only replace the local geometry.
			ScenePath path = MakeDevScenePath(ScenePathDevPreset::Line, worldPosition, profile);
			path.nodes.clear();
			path.segments.clear();

			std::array<glm::vec3, 4> nodePositions;
			if (mixedSegments)
				nodePositions = {{
					glm::vec3(-1.5f, 0.0f, -0.55f), glm::vec3(-0.5f, 0.0f, -0.55f),
					glm::vec3(0.5f, 0.0f, 0.55f), glm::vec3(1.5f, 0.0f, 0.55f)}};
			else
				nodePositions = {{
					glm::vec3(-1.5f, 0.0f, -0.9f), glm::vec3(-0.5f, 0.0f, 0.9f),
					glm::vec3(0.5f, 0.0f, -0.9f), glm::vec3(1.5f, 0.0f, 0.9f)}};
			for (const glm::vec3 &position : nodePositions)
			{
				PathNode node;
				node.id = AllocateElementId(path);
				node.position = position;
				path.nodes.push_back(node);
			}

			if (mixedSegments)
			{
				path.segments.push_back({AllocateElementId(path), LineSegmentData{}});
				path.segments.push_back(
					MakeDevCubicSegment(path, glm::vec3(0.55f, 0.0f, 0.0f), glm::vec3(-0.55f, 0.0f, 0.0f)));

				PathSegment arc;
				arc.id = AllocateElementId(path);
				CircularArcSegmentData arcData;
				arcData.planeNormal = glm::vec3(0.0f, 1.0f, 0.0f);
				arcData.signedSweepRadians = glm::half_pi<float>();
				arc.data = arcData;
				path.segments.push_back(std::move(arc));
			}
			else
			{
				for (int index = 0; index < 3; ++index)
					path.segments.push_back(
						MakeDevCubicSegment(path, glm::vec3(0.65f, 0.0f, 0.0f), glm::vec3(-0.65f, 0.0f, 0.0f)));
			}

			MovePathOriginToCentre(path);
			return path;
		}
	} // namespace

	void AddScenePathDecorationGallery(RendererWindowState &windowState, const glm::vec3 &worldPosition)
	{
		AddDecorationGalleryImpl(windowState, worldPosition);
	}

	void DrawScenePathDevAddMenu(RendererWindowState &windowState, const glm::vec3 &worldPosition)
	{
		if (!ImGui::BeginMenu("Path (dev)"))
			return;

		// The same sink every other scene-object edit already uses; without it AddScenePath applies
		// the edit and records no history, which is exactly what a dev menu must not do.
		const PathEditContext context = MakeWindowPathEditContext(windowState);
		if (ImGui::MenuItem("Decoration gallery"))
			AddScenePathDecorationGallery(windowState, worldPosition);
		if (ImGui::MenuItem("Cubic S-curve"))
			(void)AddScenePath(context, MakeDevCompositePath(worldPosition, StrokeProfile::Round, false));
		if (ImGui::MenuItem("Mixed segments"))
			(void)AddScenePath(context, MakeDevCompositePath(worldPosition, StrokeProfile::Round, true));
		if (ImGui::MenuItem("Thick curved Flat ribbon"))
		{
			ScenePath path = MakeDevScenePath(ScenePathDevPreset::Cubic, worldPosition, StrokeProfile::Flat);
			path.name = "Thick Flat ribbon";
			path.style.ribbonThickness = 0.12f;
			(void)AddScenePath(context, std::move(path));
		}

		const auto add = [&](const ScenePathDevPreset preset, const StrokeProfile profile, const char *label) {
			if (!ImGui::MenuItem(label))
				return;
			(void)AddScenePath(context, MakeDevScenePath(preset, worldPosition, profile));
		};
		const std::array<std::pair<StrokeProfile, const char *>, 3> profiles = {{
			{StrokeProfile::Round, "Tube 3D"}, {StrokeProfile::Flat, "Flat ribbon"},
			{StrokeProfile::CameraFacing, "Camera-facing ribbon"}}};
		for (const auto &[profile, profileLabel] : profiles)
		{
			if (!ImGui::BeginMenu(profileLabel))
				continue;
			add(ScenePathDevPreset::Line, profile, "Line");
			add(ScenePathDevPreset::Cubic, profile, "Cubic");
			add(ScenePathDevPreset::Arc, profile, "Arc");
			ImGui::EndMenu();
		}
		ImGui::EndMenu();
	}
} // namespace DefectStudio
