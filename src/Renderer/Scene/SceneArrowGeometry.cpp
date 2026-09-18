#include "Core/dspch.hpp"

#include "Renderer/Scene/SceneArrowGeometry.hpp"

#include <algorithm>
#include <bit>
#include <cmath>

#include "Renderer/Scene/SceneObjectAppearance.hpp"

namespace DefectStudio
{
namespace
{
	constexpr float kEpsilon = 0.0001f;
	constexpr float kTwoPi = 6.283185307f;

	[[nodiscard]] glm::vec3 SafeDirection(const glm::vec3 &from, const glm::vec3 &to)
	{
		const glm::vec3 delta = to - from;
		const float length = glm::length(delta);
		return std::isfinite(length) && length > kEpsilon ? delta / length : glm::vec3(0.0f);
	}

	void MakeBasis(const glm::vec3 &axis, glm::vec3 &outX, glm::vec3 &outY)
	{
		glm::vec3 helper = std::abs(axis.y) < 0.97f ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
		outX = glm::normalize(glm::cross(helper, axis));
		outY = glm::normalize(glm::cross(axis, outX));
	}

	void AppendCylinder(
		SceneArrowMeshData &mesh,
		const glm::vec3 &start,
		const glm::vec3 &end,
		float radius,
		float startT,
		float endT,
		std::uint32_t radialSegments)
	{
		const glm::vec3 axis = SafeDirection(start, end);
		if (glm::dot(axis, axis) <= kEpsilon * kEpsilon || radius <= kEpsilon)
			return;
		glm::vec3 basisX(0.0f), basisY(0.0f);
		MakeBasis(axis, basisX, basisY);
		const std::uint32_t startRing = static_cast<std::uint32_t>(mesh.positions.size());
		for (std::uint32_t index = 0; index < radialSegments; ++index)
		{
			const float angle = kTwoPi * static_cast<float>(index) / static_cast<float>(radialSegments);
			const glm::vec3 normal = std::cos(angle) * basisX + std::sin(angle) * basisY;
			mesh.positions.push_back(start + radius * normal);
			mesh.normals.push_back(normal);
			mesh.gradientT.push_back(startT);
			mesh.positions.push_back(end + radius * normal);
			mesh.normals.push_back(normal);
			mesh.gradientT.push_back(endT);
		}
		for (std::uint32_t index = 0; index < radialSegments; ++index)
		{
			const std::uint32_t next = (index + 1u) % radialSegments;
			const std::uint32_t a = startRing + index * 2u;
			const std::uint32_t b = a + 1u;
			const std::uint32_t c = startRing + next * 2u;
			const std::uint32_t d = c + 1u;
			mesh.indices.insert(mesh.indices.end(), {a, b, d, a, d, c});
		}

		auto appendCap = [&](const glm::vec3 &center, const glm::vec3 &normal, std::uint32_t ringOffset, float t, bool reverse) {
			const std::uint32_t centerIndex = static_cast<std::uint32_t>(mesh.positions.size());
			mesh.positions.push_back(center);
			mesh.normals.push_back(normal);
			mesh.gradientT.push_back(t);
			for (std::uint32_t index = 0; index < radialSegments; ++index)
			{
				const std::uint32_t next = (index + 1u) % radialSegments;
				const std::uint32_t a = ringOffset + index * 2u;
				const std::uint32_t b = ringOffset + next * 2u;
				if (reverse)
					mesh.indices.insert(mesh.indices.end(), {centerIndex, b, a});
				else
					mesh.indices.insert(mesh.indices.end(), {centerIndex, a + 1u, b + 1u});
			}
		};
		appendCap(start, -axis, startRing, startT, true);
		appendCap(end, axis, startRing, endT, false);
	}

	void AppendCone(
		SceneArrowMeshData &mesh,
		const glm::vec3 &tip,
		const glm::vec3 &inward,
		float length,
		float radius,
		float gradientT,
		std::uint32_t radialSegments,
		bool closeBack,
		float bulgeStrength)
	{
		if (glm::dot(inward, inward) <= kEpsilon * kEpsilon || length <= kEpsilon || radius <= kEpsilon)
			return;
		glm::vec3 basisX(0.0f), basisY(0.0f);
		MakeBasis(inward, basisX, basisY);
		const glm::vec3 base = tip + inward * length;
		const float bulge = 1.0f + std::clamp(bulgeStrength, 0.0f, 1.0f) * 0.12f;
		const std::uint32_t first = static_cast<std::uint32_t>(mesh.positions.size());
		for (std::uint32_t index = 0; index < radialSegments; ++index)
		{
			const float angle = kTwoPi * static_cast<float>(index) / static_cast<float>(radialSegments);
			const glm::vec3 radial = std::cos(angle) * basisX + std::sin(angle) * basisY;
			const glm::vec3 normal = glm::normalize(radial - inward * (radius / length));
			mesh.positions.push_back(base + radius * bulge * radial);
			mesh.normals.push_back(normal);
			mesh.gradientT.push_back(gradientT);
			mesh.positions.push_back(tip);
			mesh.normals.push_back(normal);
			mesh.gradientT.push_back(gradientT);
		}
		for (std::uint32_t index = 0; index < radialSegments; ++index)
		{
			const std::uint32_t next = (index + 1u) % radialSegments;
			mesh.indices.insert(mesh.indices.end(), {first + index * 2u, first + index * 2u + 1u, first + next * 2u});
		}
		if (!closeBack)
			return;
		const std::uint32_t center = static_cast<std::uint32_t>(mesh.positions.size());
		mesh.positions.push_back(base);
		mesh.normals.push_back(inward);
		mesh.gradientT.push_back(gradientT);
		for (std::uint32_t index = 0; index < radialSegments; ++index)
		{
			const std::uint32_t next = (index + 1u) % radialSegments;
			mesh.indices.insert(mesh.indices.end(), {center, first + next * 2u, first + index * 2u});
		}
	}

	void AppendSphere(
		SceneArrowMeshData &mesh,
		const glm::vec3 &center,
		float radius,
		float gradientT,
		std::uint32_t radialSegments)
	{
		const std::uint32_t rings = std::max(radialSegments / 2u, 3u);
		const std::uint32_t first = static_cast<std::uint32_t>(mesh.positions.size());
		for (std::uint32_t latitude = 0; latitude <= rings; ++latitude)
		{
			const float phi = 3.141592654f * static_cast<float>(latitude) / static_cast<float>(rings);
			for (std::uint32_t longitude = 0; longitude < radialSegments; ++longitude)
			{
				const float theta = kTwoPi * static_cast<float>(longitude) / static_cast<float>(radialSegments);
				const glm::vec3 normal(
					std::sin(phi) * std::cos(theta), std::cos(phi), std::sin(phi) * std::sin(theta));
				mesh.positions.push_back(center + radius * normal);
				mesh.normals.push_back(normal);
				mesh.gradientT.push_back(gradientT);
			}
		}
		for (std::uint32_t latitude = 0; latitude < rings; ++latitude)
		{
			for (std::uint32_t longitude = 0; longitude < radialSegments; ++longitude)
			{
				const std::uint32_t next = (longitude + 1u) % radialSegments;
				const std::uint32_t a = first + latitude * radialSegments + longitude;
				const std::uint32_t b = first + (latitude + 1u) * radialSegments + longitude;
				const std::uint32_t c = first + latitude * radialSegments + next;
				const std::uint32_t d = first + (latitude + 1u) * radialSegments + next;
				mesh.indices.insert(mesh.indices.end(), {a, b, d, a, d, c});
			}
		}
	}

	void AppendTip(
		SceneArrowMeshData &mesh,
		RendererWindowState::ArrowTip tipStyle,
		const glm::vec3 &tip,
		const glm::vec3 &inward,
		float baseLength,
		float baseWidth,
		float shaftRadius,
		float maximumLength,
		float gradientT,
		std::uint32_t radialSegments,
		float bulgeStrength)
	{
		const ArrowTipParameters parameters = GetArrowTipParameters(tipStyle);
		if (!parameters.producesGeometry())
			return;
		const float length = std::min(
			std::max(baseLength, 0.0f) * parameters.lengthScale, std::max(maximumLength, 0.0f));
		const float width = std::max(baseWidth, 0.0f) * parameters.widthScale;
		using Tip = RendererWindowState::ArrowTip;
		if (tipStyle == Tip::Circle)
		{
			AppendSphere(mesh, tip, width * 0.5f, gradientT, radialSegments);
			return;
		}
		if (glm::dot(inward, inward) <= kEpsilon * kEpsilon)
			return;
		glm::vec3 basisX(0.0f), basisY(0.0f);
		MakeBasis(inward, basisX, basisY);
		if (tipStyle == Tip::Bar)
		{
			AppendCylinder(
				mesh, tip - basisX * width * 0.5f, tip + basisX * width * 0.5f,
				std::max(shaftRadius * 0.65f, length * 0.5f), gradientT, gradientT, radialSegments);
			return;
		}
		if (!parameters.filled)
		{
			const glm::vec3 back = tip + inward * length;
			const float strokeRadius = std::max(shaftRadius * 0.4f, width * 0.045f);
			AppendCylinder(mesh, tip, back + basisX * width * 0.5f, strokeRadius, gradientT, gradientT, radialSegments);
			AppendCylinder(mesh, tip, back - basisX * width * 0.5f, strokeRadius, gradientT, gradientT, radialSegments);
			return;
		}
		AppendCone(
			mesh, tip, inward, length, width * 0.5f, gradientT, radialSegments,
			parameters.closesBack, bulgeStrength);
	}
} // namespace

	SceneArrowPath TessellateSceneArrowPath(const RendererWindowState::SceneArrow &arrow)
	{
		SceneArrowPath path;
		if (arrow.points.size() < 2)
			return path;
		if (arrow.points.size() == 2 && arrow.controlPoint)
		{
			const int segments = std::max(arrow.curveSegments, 1);
			path.points.reserve(static_cast<std::size_t>(segments) + 1u);
			for (int index = 0; index <= segments; ++index)
			{
				const float t = static_cast<float>(index) / static_cast<float>(segments);
				const float oneMinusT = 1.0f - t;
				path.points.push_back(
					oneMinusT * oneMinusT * arrow.points[0] +
					2.0f * oneMinusT * t * *arrow.controlPoint + t * t * arrow.points[1]);
			}
		}
		else
		{
			path.points = arrow.points;
		}
		path.cumulativeLengths.resize(path.points.size(), 0.0f);
		for (std::size_t index = 1; index < path.points.size(); ++index)
		{
			const float segmentLength = glm::distance(path.points[index - 1], path.points[index]);
			if (std::isfinite(segmentLength))
				path.totalLength += segmentLength;
			path.cumulativeLengths[index] = path.totalLength;
		}
		return path;
	}

	std::vector<SceneArrowShaftSpan> BuildSceneArrowShaftSpans(
		const SceneArrowPath &path,
		bool dashed,
		float dashLength,
		float gapLength,
		float startInset,
		float endInset)
	{
		std::vector<SceneArrowShaftSpan> result;
		if (path.points.size() < 2 || path.cumulativeLengths.size() != path.points.size() || path.totalLength <= kEpsilon)
			return result;
		const float activeStart = std::clamp(startInset, 0.0f, path.totalLength);
		const float activeEnd = std::clamp(path.totalLength - endInset, activeStart, path.totalLength);
		const std::vector<SceneArrowShaftSegment> dashSegments =
			BuildSceneArrowShaftSegments(path.totalLength, dashed, dashLength, gapLength);
		for (const SceneArrowShaftSegment &dash : dashSegments)
		{
			const float dashStart = std::max(dash.start, activeStart);
			const float dashEnd = std::min(dash.end, activeEnd);
			if (dashEnd - dashStart <= kEpsilon)
				continue;
			for (std::size_t index = 1; index < path.points.size(); ++index)
			{
				const float segmentStart = path.cumulativeLengths[index - 1];
				const float segmentEnd = path.cumulativeLengths[index];
				const float overlapStart = std::max(dashStart, segmentStart);
				const float overlapEnd = std::min(dashEnd, segmentEnd);
				const float segmentLength = segmentEnd - segmentStart;
				if (overlapEnd - overlapStart <= kEpsilon || segmentLength <= kEpsilon)
					continue;
				const float startT = (overlapStart - segmentStart) / segmentLength;
				const float endT = (overlapEnd - segmentStart) / segmentLength;
				result.push_back({
					glm::mix(path.points[index - 1], path.points[index], startT),
					glm::mix(path.points[index - 1], path.points[index], endT),
					overlapStart,
					overlapEnd});
			}
		}
		return result;
	}

	SceneArrowMeshData BuildSceneArrowMesh(
		const RendererWindowState::SceneArrow &arrow, std::uint32_t radialSegments, float bulgeStrength)
	{
		SceneArrowMeshData mesh;
		mesh.path = TessellateSceneArrowPath(arrow);
		if (mesh.path.points.size() < 2 || mesh.path.totalLength <= kEpsilon)
			return mesh;
		radialSegments = std::max(radialSegments, 3u);
		const ArrowTipParameters startParameters = GetArrowTipParameters(arrow.startTip);
		const ArrowTipParameters endParameters = GetArrowTipParameters(arrow.endTip);
		const float startInset = startParameters.producesGeometry()
			? std::min(arrow.style.headLength * startParameters.lengthScale, mesh.path.totalLength * 0.45f)
			: 0.0f;
		const float endInset = endParameters.producesGeometry()
			? std::min(arrow.style.headLength * endParameters.lengthScale, mesh.path.totalLength * 0.45f)
			: 0.0f;
		const float shaftRadius = std::max(arrow.style.shaftWidth, 0.0f) * 0.5f;
		for (const SceneArrowShaftSpan &span : BuildSceneArrowShaftSpans(
				 mesh.path, arrow.style.dashed, arrow.style.dashLength, arrow.style.gapLength,
				 startInset, endInset))
		{
			AppendCylinder(
				mesh, span.start, span.end, shaftRadius,
				span.startDistance / mesh.path.totalLength, span.endDistance / mesh.path.totalLength,
				radialSegments);
		}

		const glm::vec3 startInward = SafeDirection(mesh.path.points[0], mesh.path.points[1]);
		const glm::vec3 endInward = SafeDirection(mesh.path.points.back(), mesh.path.points[mesh.path.points.size() - 2]);
		AppendTip(
			mesh, arrow.startTip, mesh.path.points.front(), startInward, arrow.style.headLength,
			arrow.style.headWidth, shaftRadius, mesh.path.totalLength * 0.45f,
			0.0f, radialSegments, bulgeStrength);
		AppendTip(
			mesh, arrow.endTip, mesh.path.points.back(), endInward, arrow.style.headLength,
			arrow.style.headWidth, shaftRadius, mesh.path.totalLength * 0.45f,
			1.0f, radialSegments, bulgeStrength);
		return mesh;
	}

	std::uint64_t SceneArrowGeometryHash(
		const RendererWindowState::SceneArrow &arrow, const float bulgeStrength)
	{
		std::uint64_t hash = 1469598103934665603ull;
		auto append = [&hash](const std::uint64_t value) {
			hash ^= value;
			hash *= 1099511628211ull;
		};
		auto appendFloat = [&append](const float value) { append(std::bit_cast<std::uint32_t>(value)); };
		for (const glm::vec3 &point : arrow.points)
		{
			appendFloat(point.x);
			appendFloat(point.y);
			appendFloat(point.z);
		}
		append(arrow.controlPoint.has_value());
		if (arrow.controlPoint)
		{
			appendFloat(arrow.controlPoint->x);
			appendFloat(arrow.controlPoint->y);
			appendFloat(arrow.controlPoint->z);
		}
		append(static_cast<std::uint64_t>(arrow.curveSegments));
		append(static_cast<std::uint64_t>(arrow.startTip));
		append(static_cast<std::uint64_t>(arrow.endTip));
		appendFloat(arrow.style.shaftWidth);
		append(arrow.style.dashed);
		appendFloat(arrow.style.dashLength);
		appendFloat(arrow.style.gapLength);
		appendFloat(arrow.style.headWidth);
		appendFloat(arrow.style.headLength);
		appendFloat(bulgeStrength);
		return hash;
	}
} // namespace DefectStudio
