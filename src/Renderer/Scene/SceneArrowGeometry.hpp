#pragma once

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

#include "Renderer/RendererWindowState.hpp"

namespace DefectStudio
{
	struct ArrowTipParameters
	{
		float lengthScale = 0.0f;
		float widthScale = 0.0f;
		bool filled = false;
		bool closesBack = false;

		[[nodiscard]] constexpr bool producesGeometry() const
		{
			return lengthScale > 0.0f && widthScale > 0.0f;
		}
	};

	// The single renderer-owned vocabulary table. All arrow renderers consume these proportions;
	// IO persists only the stable enum names and has no knowledge of their shapes.
	[[nodiscard]] constexpr ArrowTipParameters GetArrowTipParameters(RendererWindowState::ArrowTip tip)
	{
		using Tip = RendererWindowState::ArrowTip;
		switch (tip)
		{
			case Tip::None: return {};
			case Tip::Plain: return {1.0f, 1.0f, true, true};
			case Tip::Barbed: return {1.15f, 1.10f, true, false};
			case Tip::Open: return {1.0f, 1.0f, false, false};
			case Tip::Bar: return {0.08f, 1.25f, true, true};
			case Tip::Circle: return {0.75f, 0.75f, true, true};
		}
		return {};
	}

	struct SceneArrowPath
	{
		std::vector<glm::vec3> points;
		std::vector<float> cumulativeLengths;
		float totalLength = 0.0f;
	};

	struct SceneArrowShaftSpan
	{
		glm::vec3 start = glm::vec3(0.0f);
		glm::vec3 end = glm::vec3(0.0f);
		float startDistance = 0.0f;
		float endDistance = 0.0f;
	};

	struct SceneArrowMeshData
	{
		std::vector<glm::vec3> positions;
		std::vector<glm::vec3> normals;
		std::vector<float> gradientT;
		std::vector<std::uint32_t> indices;
		SceneArrowPath path;
	};

	[[nodiscard]] SceneArrowPath TessellateSceneArrowPath(const RendererWindowState::SceneArrow &arrow);
	[[nodiscard]] std::vector<SceneArrowShaftSpan> BuildSceneArrowShaftSpans(
		const SceneArrowPath &path,
		bool dashed,
		float dashLength,
		float gapLength,
		float startInset,
		float endInset);
	[[nodiscard]] SceneArrowMeshData BuildSceneArrowMesh(
		const RendererWindowState::SceneArrow &arrow,
		std::uint32_t radialSegments = 24u,
		float bulgeStrength = 0.0f);
	[[nodiscard]] std::uint64_t SceneArrowGeometryHash(
		const RendererWindowState::SceneArrow &arrow, float bulgeStrength);
} // namespace DefectStudio
