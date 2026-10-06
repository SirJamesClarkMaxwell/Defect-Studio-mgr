#include "Core/dspch.hpp"

#include <utility>

#include "Presentation/Panels/ScenePathDevMenu.hpp"
#include "Presentation/Panels/ScenePathOperations.hpp"

namespace DefectStudio
{
	float &GetScenePathAtomBuffer()
	{
		static float buffer = 1.15f;
		return buffer;
	}

	Result<SceneObjectId> AddFreeScenePathSegment(
		RendererWindowState &windowState, const glm::vec3 &worldPosition, const bool arrow)
	{
		const float length = GetDefaultSceneSegmentLength(windowState);
		ScenePath path = MakeDevScenePath(ScenePathDevPreset::Line, worldPosition + glm::vec3(length * 0.5f, 0, 0));
		path.name = arrow ? "Arrow" : "Line";
		path.nodes.front().position = glm::vec3(-length * 0.5f, 0, 0);
		path.nodes.back().position = glm::vec3(length * 0.5f, 0, 0);
		path.style.endDecoration.kind = arrow ? PathDecorationKind::Arrow : PathDecorationKind::None;
		const auto added = AddScenePath(MakeWindowPathEditContext(windowState), std::move(path));
		if (added)
			SelectAddedScenePaths(windowState, {added.Value()});
		return added;
	}
}
