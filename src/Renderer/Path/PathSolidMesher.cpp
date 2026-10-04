#include "Core/dspch.hpp"

#include "Renderer/Path/PathSolidMesher.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <utility>

namespace DefectStudio::detail
{
	namespace
	{
		constexpr double kWeldToleranceSquared = 1.0e-20;

		[[nodiscard]] glm::dvec3 SafeNormal(const glm::dvec3 &value, const glm::dvec3 &fallback)
		{
			return glm::dot(value, value) > kWeldToleranceSquared ? glm::normalize(value) : fallback;
		}

		[[nodiscard]] std::uint32_t AddVertex(ThickFlatMesh &mesh, const glm::dvec3 &position,
			const PathStrokeStyle &style, const double normalizedT, const double arcLength,
			const glm::vec4 *color = nullptr)
		{
			for (std::uint32_t index = 0u; index < mesh.vertices.size(); ++index)
				if (glm::dot(mesh.vertices[index].position - position, mesh.vertices[index].position - position) <=
					kWeldToleranceSquared)
					return index;
			mesh.vertices.push_back({position, color != nullptr ? *color : SampleStrokeColor(style, normalizedT),
				static_cast<float>(normalizedT), static_cast<float>(arcLength)});
			return static_cast<std::uint32_t>(mesh.vertices.size() - 1u);
		}

		void AddFace(ThickFlatMesh &mesh, std::vector<std::uint32_t> vertices,
			const glm::dvec3 &expectedNormal, const ThickFlatFaceOwner owner,
			std::vector<bool> bevelEdges = {})
		{
			std::vector<std::uint32_t> compact;
			std::vector<bool> compactBevels;
			compact.reserve(vertices.size());
			compactBevels.reserve(vertices.size());
			if (bevelEdges.size() != vertices.size())
				bevelEdges.assign(vertices.size(), true);
			for (std::size_t index = 0u; index < vertices.size(); ++index)
			{
				const std::uint32_t vertex = vertices[index];
				if (compact.empty() || compact.back() != vertex)
				{
					compact.push_back(vertex);
					compactBevels.push_back(bevelEdges[index]);
				}
			}
			if (compact.size() > 1u && compact.front() == compact.back())
			{
				compact.pop_back();
				compactBevels.pop_back();
			}
			if (compact.size() < 3u)
				return;

			glm::dvec3 normal(0.0);
			for (std::size_t index = 0u; index < compact.size(); ++index)
			{
				const glm::dvec3 &a = mesh.vertices[compact[index]].position;
				const glm::dvec3 &b = mesh.vertices[compact[(index + 1u) % compact.size()]].position;
				normal += glm::cross(a, b);
			}
			if (glm::dot(normal, normal) <= kWeldToleranceSquared)
				return;
			if (glm::dot(normal, expectedNormal) < 0.0)
			{
				std::reverse(compact.begin(), compact.end());
				std::reverse(compactBevels.begin(), compactBevels.end());
				std::rotate(compactBevels.begin(), compactBevels.begin() + 1u, compactBevels.end());
			}
			mesh.faces.push_back({std::move(compact), std::move(compactBevels), owner});
		}

		struct DecorationLoop
		{
			std::vector<glm::dvec3> positions;
			std::size_t attachmentEdge = 0u;
		};

		[[nodiscard]] DecorationLoop BuildAttachedDecorationLoop(const DecorationContour &contour,
			const EvaluatedSample &endpoint, const glm::dvec3 &inward, const double shaftHalfWidth,
			const bool attached)
		{
			DecorationLoop result;
			if (contour.points.empty())
				return result;
			const double backS = contour.points.back().s;
			double backHalfWidth = attached ? shaftHalfWidth : 0.0;
			for (const DecorationContourPoint &point : contour.points)
				if (point.s == backS)
					backHalfWidth = std::max(backHalfWidth, point.halfWidth);

			std::vector<DecorationContourPoint> side;
			for (const DecorationContourPoint &point : contour.points)
				if (point.s < backS)
					side.push_back(point);
			side.push_back({backS, backHalfWidth});

			const auto append = [&](const DecorationContourPoint &point, const double sign) {
				const glm::dvec3 position = endpoint.position + inward * point.s + endpoint.normal * sign * point.halfWidth;
				if (result.positions.empty() || glm::dot(result.positions.back() - position,
					result.positions.back() - position) > kWeldToleranceSquared)
					result.positions.push_back(position);
			};
			for (const DecorationContourPoint &point : side)
				append(point, 1.0);
			if (attached && backHalfWidth > shaftHalfWidth)
				append({backS, shaftHalfWidth}, 1.0);
			result.attachmentEdge = result.positions.empty() ? 0u : result.positions.size() - 1u;
			if (attached)
				append({backS, shaftHalfWidth}, -1.0);
			for (auto point = side.rbegin(); point != side.rend(); ++point)
				append(*point, -1.0);
			if (result.positions.size() > 1u && glm::dot(result.positions.front() - result.positions.back(),
				result.positions.front() - result.positions.back()) <= kWeldToleranceSquared)
				result.positions.pop_back();
		return result;
		}

		struct OwnerOutput
		{
			std::vector<StrokeTubeVertex> vertices;
			std::vector<std::uint32_t> indices;
		};

		[[nodiscard]] std::size_t OwnerIndex(const ThickFlatFaceOwner owner)
		{
			return static_cast<std::size_t>(owner);
		}

		[[nodiscard]] glm::dvec3 FaceNormal(const ThickFlatMesh &mesh, const ThickFlatMeshFace &face)
		{
			glm::dvec3 normal(0.0);
			for (std::size_t index = 0u; index < face.vertices.size(); ++index)
				normal += glm::cross(mesh.vertices[face.vertices[index]].position,
					mesh.vertices[face.vertices[(index + 1u) % face.vertices.size()]].position);
			return SafeNormal(normal, glm::dvec3(0.0));
		}

		struct SeamIncident
		{
			std::size_t face = 0u;
			std::size_t corner = 0u;
		};

		[[nodiscard]] bool MergeFacesAcross(ThickFlatMesh &mesh, const SeamIncident &firstIncident,
			const SeamIncident &secondIncident)
		{
			const std::size_t keep = std::min(firstIncident.face, secondIncident.face);
			const std::size_t remove = std::max(firstIncident.face, secondIncident.face);
			const std::size_t firstCorner = keep == firstIncident.face ? firstIncident.corner : secondIncident.corner;
			const std::size_t secondCorner = keep == firstIncident.face ? secondIncident.corner : firstIncident.corner;
			const ThickFlatMeshFace &first = mesh.faces[keep];
			const ThickFlatMeshFace &second = mesh.faces[remove];
			const std::uint32_t from = first.vertices[firstCorner];
			const std::uint32_t to = first.vertices[(firstCorner + 1u) % first.vertices.size()];
			if (second.vertices[secondCorner] != to ||
				second.vertices[(secondCorner + 1u) % second.vertices.size()] != from)
				return false;

			ThickFlatMeshFace merged;
			merged.owner = first.owner != ThickFlatFaceOwner::Shaft ? first.owner : second.owner;
			for (std::size_t offset = 1u; offset <= first.vertices.size(); ++offset)
			{
				const std::size_t index = (firstCorner + offset) % first.vertices.size();
				merged.vertices.push_back(first.vertices[index]);
				if (offset < first.vertices.size())
					merged.bevelEdges.push_back(first.bevelEdges[index]);
			}
			merged.bevelEdges.push_back(second.bevelEdges[(secondCorner + 1u) % second.vertices.size()]);
			for (std::size_t offset = 2u; offset < second.vertices.size(); ++offset)
			{
				const std::size_t index = (secondCorner + offset) % second.vertices.size();
				merged.vertices.push_back(second.vertices[index]);
				merged.bevelEdges.push_back(second.bevelEdges[index]);
			}
			mesh.faces[keep] = std::move(merged);
			mesh.faces.erase(mesh.faces.begin() + static_cast<std::ptrdiff_t>(remove));
			return true;
		}

		void EmitSharpFace(const ThickFlatMesh &mesh, const ThickFlatMeshFace &face, OwnerOutput &output)
		{
			glm::dvec3 normal(0.0);
			for (std::size_t index = 0u; index < face.vertices.size(); ++index)
				normal += glm::cross(mesh.vertices[face.vertices[index]].position,
					mesh.vertices[face.vertices[(index + 1u) % face.vertices.size()]].position);
			normal = SafeNormal(normal, glm::dvec3(0.0, 0.0, 1.0));
			for (std::size_t index = 1u; index + 1u < face.vertices.size(); ++index)
			{
				for (const std::size_t corner : {std::size_t{0u}, index, index + 1u})
				{
					const ThickFlatMeshVertex &source = mesh.vertices[face.vertices[corner]];
					output.indices.push_back(static_cast<std::uint32_t>(output.vertices.size()));
					output.vertices.push_back({glm::vec3(source.position), glm::vec3(normal), source.color,
						source.arcT, source.dashCoord});
				}
			}
		}
	} // namespace

	bool UsesThickFlatSolidBevel(const PathStrokeStyle &style,
		const DecorationContour &startContour, const DecorationContour &endContour)
	{
		const auto attachable = [](const DecorationContour &contour) {
			return contour.points.empty() || (contour.filled && contour.closesBack);
		};
		return style.profile == StrokeProfile::Flat && std::isfinite(style.ribbonThickness) &&
			style.ribbonThickness > 0.0f && std::isfinite(style.ribbonBevel) && style.ribbonBevel > 0.0f &&
			attachable(startContour) && attachable(endContour);
	}

	void MergeCoplanarThickFlatSeams(ThickFlatMesh &mesh)
	{
		bool changed = true;
		while (changed)
		{
			changed = false;
			std::map<std::pair<std::uint32_t, std::uint32_t>, std::vector<SeamIncident>> edges;
			for (std::size_t faceIndex = 0u; faceIndex < mesh.faces.size(); ++faceIndex)
				for (std::size_t corner = 0u; corner < mesh.faces[faceIndex].vertices.size(); ++corner)
				{
					const std::uint32_t first = mesh.faces[faceIndex].vertices[corner];
					const std::uint32_t second = mesh.faces[faceIndex].vertices[
						(corner + 1u) % mesh.faces[faceIndex].vertices.size()];
					edges[{std::min(first, second), std::max(first, second)}].push_back({faceIndex, corner});
				}
			for (const auto &[edge, incidents] : edges)
			{
				if (incidents.size() != 2u || mesh.faces[incidents[0].face].bevelEdges[incidents[0].corner] ||
					mesh.faces[incidents[1].face].bevelEdges[incidents[1].corner])
					continue;
				if (glm::dot(FaceNormal(mesh, mesh.faces[incidents[0].face]),
					FaceNormal(mesh, mesh.faces[incidents[1].face])) < 1.0 - 1.0e-10)
					continue;
				if (MergeFacesAcross(mesh, incidents[0], incidents[1]))
				{
					changed = true;
					break;
				}
			}
		}
	}

	void AppendThickFlatPiece(ThickFlatMesh &mesh, const std::vector<EvaluatedSample> &samples,
		const PathStrokeStyle &style, const bool capStart, const bool capEnd,
		const std::vector<glm::vec4> *colors)
	{
		if (samples.size() < 2u)
			return;
		std::vector<EvaluatedSample> working = samples;
		const double halfWidth = static_cast<double>(style.width) * 0.5;
		const double halfThickness = static_cast<double>(style.ribbonThickness) * 0.5;
		if (style.cap == PathLineCap::Square)
		{
			if (capStart)
				working.front().position -= working.front().tangent * halfWidth;
			if (capEnd)
				working.back().position += working.back().tangent * halfWidth;
		}
		std::vector<std::array<std::uint32_t, 4u>> rings;
		rings.reserve(working.size());
		for (std::size_t index = 0u; index < working.size(); ++index)
		{
			const EvaluatedSample &sample = working[index];
			const std::array<glm::dvec3, 4u> offsets = {
				sample.normal * halfWidth + sample.binormal * halfThickness,
				-sample.normal * halfWidth + sample.binormal * halfThickness,
				-sample.normal * halfWidth - sample.binormal * halfThickness,
				sample.normal * halfWidth - sample.binormal * halfThickness};
			std::array<std::uint32_t, 4u> ring{};
			for (std::size_t corner = 0u; corner < offsets.size(); ++corner)
				ring[corner] = AddVertex(mesh, sample.position + offsets[corner], style,
					sample.normalizedT, sample.arcLength,
					colors != nullptr && colors->size() == working.size() ? &(*colors)[index] : nullptr);
			rings.push_back(ring);
		}
		for (std::size_t index = 0u; index + 1u < rings.size(); ++index)
		{
			const glm::dvec3 normal = SafeNormal(working[index].normal + working[index + 1u].normal, working[index].normal);
			const glm::dvec3 binormal = SafeNormal(working[index].binormal + working[index + 1u].binormal, working[index].binormal);
			const std::array<glm::dvec3, 4u> hints = {binormal, -normal, -binormal, normal};
			const bool bevelStart = capStart && index == 0u;
			const bool bevelEnd = capEnd && index + 2u == rings.size();
			for (std::size_t edge = 0u; edge < 4u; ++edge)
				AddFace(mesh, {rings[index][edge], rings[index + 1u][edge],
					rings[index + 1u][(edge + 1u) % 4u], rings[index][(edge + 1u) % 4u]},
					hints[edge], ThickFlatFaceOwner::Shaft, {true, bevelEnd, true, bevelStart});
		}
		if (capStart)
			AddFace(mesh, {rings.front()[3], rings.front()[2], rings.front()[1], rings.front()[0]},
				-working.front().tangent, ThickFlatFaceOwner::Shaft);
		if (capEnd)
			AddFace(mesh, {rings.back()[0], rings.back()[1], rings.back()[2], rings.back()[3]},
				working.back().tangent, ThickFlatFaceOwner::Shaft);
	}

	void AppendAttachedThickFlatDecoration(ThickFlatMesh &mesh, const DecorationContour &contour,
		const EvaluatedSample &endpoint, const bool start, const PathStrokeStyle &style,
		const ThickFlatFaceOwner owner, const bool attachedToShaft)
	{
		if (contour.points.empty())
			return;
		const glm::dvec3 inward = start ? endpoint.tangent : -endpoint.tangent;
		const DecorationLoop loop = BuildAttachedDecorationLoop(contour, endpoint, inward,
			static_cast<double>(style.width) * 0.5, attachedToShaft);
		if (loop.positions.size() < 3u)
			return;
		const double halfThickness = static_cast<double>(style.ribbonThickness) * 0.5;
		std::vector<std::uint32_t> front;
		std::vector<std::uint32_t> back;
		front.reserve(loop.positions.size());
		back.reserve(loop.positions.size());
		for (const glm::dvec3 &position : loop.positions)
		{
			front.push_back(AddVertex(mesh, position + endpoint.binormal * halfThickness, style,
				endpoint.normalizedT, endpoint.arcLength));
			back.push_back(AddVertex(mesh, position - endpoint.binormal * halfThickness, style,
				endpoint.normalizedT, endpoint.arcLength));
		}
		std::vector<bool> outlineBevels(front.size(), true);
		if (attachedToShaft && loop.attachmentEdge < outlineBevels.size())
			outlineBevels[loop.attachmentEdge] = false;
		AddFace(mesh, front, endpoint.binormal, owner, outlineBevels);
		std::vector<std::uint32_t> reversedBack(back.rbegin(), back.rend());
		std::reverse(outlineBevels.begin(), outlineBevels.end());
		std::rotate(outlineBevels.begin(), outlineBevels.begin() + 1u, outlineBevels.end());
		AddFace(mesh, std::move(reversedBack), -endpoint.binormal, owner, std::move(outlineBevels));
		// Mirroring the contour at the end reverses its loop winding and side normals.
		const glm::dvec3 sideAxis = start ? endpoint.binormal : -endpoint.binormal;
		for (std::size_t index = 0u; index < loop.positions.size(); ++index)
		{
			if (attachedToShaft && index == loop.attachmentEdge)
				continue;
			const std::size_t next = (index + 1u) % loop.positions.size();
			const glm::dvec3 outward = SafeNormal(glm::cross(sideAxis,
				loop.positions[next] - loop.positions[index]), endpoint.normal);
			AddFace(mesh, {front[index], front[next], back[next], back[index]}, outward, owner);
		}
	}

	void FinalizeSharpThickFlatMesh(const ThickFlatMesh &mesh, StrokeGeometry &geometry)
	{
		std::array<OwnerOutput, 3u> outputs;
		for (const ThickFlatMeshFace &face : mesh.faces)
			EmitSharpFace(mesh, face, outputs[OwnerIndex(face.owner)]);

		geometry.tubeVertices.clear();
		geometry.ribbonVertices.clear();
		geometry.indices.clear();
		const auto append = [&](const ThickFlatFaceOwner owner, StrokeMeshRange &range) {
			OwnerOutput &output = outputs[OwnerIndex(owner)];
			range.firstIndex = static_cast<std::uint32_t>(geometry.indices.size());
			const std::uint32_t firstVertex = static_cast<std::uint32_t>(geometry.tubeVertices.size());
			geometry.tubeVertices.insert(geometry.tubeVertices.end(), output.vertices.begin(), output.vertices.end());
			for (const std::uint32_t index : output.indices)
				geometry.indices.push_back(firstVertex + index);
			range.indexCount = static_cast<std::uint32_t>(geometry.indices.size()) - range.firstIndex;
		};
		append(ThickFlatFaceOwner::StartDecoration, geometry.startDecoration);
		append(ThickFlatFaceOwner::EndDecoration, geometry.endDecoration);
		append(ThickFlatFaceOwner::Shaft, geometry.shaft);
	}
}
