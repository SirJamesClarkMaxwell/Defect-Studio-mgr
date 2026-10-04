#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include <glm/glm.hpp>

#include "Renderer/Path/PathStrokeMesher.hpp"

namespace DefectStudio::detail
{
	enum class ThickFlatFaceOwner : std::uint8_t
	{
		StartDecoration,
		EndDecoration,
		Shaft,
	};

	struct ThickFlatMeshVertex
	{
		glm::dvec3 position{0.0};
		glm::vec4 color{1.0f};
		float arcT = 0.0f;
		float dashCoord = 0.0f;
		// A bevel at a flat terminal cap must stay in its inward half-space.
		std::optional<glm::dvec3> capOutwardNormal;
	};

	struct ThickFlatMeshFace
	{
		std::vector<std::uint32_t> vertices;
		std::vector<bool> bevelEdges;
		ThickFlatFaceOwner owner = ThickFlatFaceOwner::Shaft;
		std::uint32_t smoothingGroup = 0;
	};

	struct ThickFlatMesh
	{
		std::vector<ThickFlatMeshVertex> vertices;
		std::vector<ThickFlatMeshFace> faces;
	};

	[[nodiscard]] bool UsesThickFlatSolidBevel(const PathStrokeStyle &style,
		const DecorationContour &startContour, const DecorationContour &endContour);
	void MergeCoplanarThickFlatSeams(ThickFlatMesh &mesh, bool preserveOwners = false);

	void AppendThickFlatPiece(ThickFlatMesh &mesh, const std::vector<EvaluatedSample> &samples,
		const PathStrokeStyle &style, bool capStart, bool capEnd,
		const std::vector<glm::vec4> *colors = nullptr);

	void AppendAttachedThickFlatDecoration(ThickFlatMesh &mesh, const DecorationContour &contour,
		const EvaluatedSample &endpoint, bool start, const PathStrokeStyle &style,
		ThickFlatFaceOwner owner, bool attachedToShaft);

	void FinalizeSharpThickFlatMesh(const ThickFlatMesh &mesh, StrokeGeometry &geometry);
	void FinalizeThickFlatMesh(const ThickFlatMesh &mesh, const PathStrokeStyle &style,
		StrokeGeometry &geometry);
}
