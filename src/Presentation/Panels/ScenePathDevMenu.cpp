#include "Core/dspch.hpp"

#include "Presentation/Panels/ScenePathDevMenu.hpp"

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
			std::vector<SceneObjectId> added;
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
				const auto result = AddScenePath(context, std::move(path));
				if (result)
					added.push_back(result.Value());
			}
			if (!added.empty())
				SelectAddedScenePaths(windowState, std::move(added));
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

	void AddScenePathDecorationGallery(RendererWindowState &windowState, const glm::vec3 &worldPosition)
	{
		AddDecorationGalleryImpl(windowState, worldPosition);
	}

} // namespace DefectStudio
