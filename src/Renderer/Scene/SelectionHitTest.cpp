#include "Core/dspch.hpp"

#include "Renderer/Scene/SelectionHitTest.hpp"

#include <algorithm>

namespace DefectStudio::SelectionHitTest
{
	std::optional<glm::vec2> ProjectToScreen(
		const glm::mat4 &viewProjection, glm::vec2 viewportSize, const glm::vec3 &worldPosition)
	{
		const glm::vec4 clip = viewProjection * glm::vec4(worldPosition, 1.0f);
		if (clip.w <= 0.0001f)
			return std::nullopt;
		const glm::vec3 ndc = glm::vec3(clip) / clip.w;
		return glm::vec2(
			(ndc.x + 1.0f) * 0.5f * viewportSize.x,
			(1.0f - ndc.y) * 0.5f * viewportSize.y);
	}

	bool PointInRect(glm::vec2 point, glm::vec2 rectMin, glm::vec2 rectMax)
	{
		return point.x >= rectMin.x && point.x <= rectMax.x &&
			point.y >= rectMin.y && point.y <= rectMax.y;
	}

	bool PointInCircle(glm::vec2 point, glm::vec2 center, float radius)
	{
		return glm::length(point - center) <= radius;
	}

	float DistancePointToSegment(glm::vec2 point, glm::vec2 segA, glm::vec2 segB)
	{
		const glm::vec2 segment = segB - segA;
		const float segmentLengthSq = glm::dot(segment, segment);
		const float t = segmentLengthSq > 0.0001f
			? std::clamp(glm::dot(point - segA, segment) / segmentLengthSq, 0.0f, 1.0f)
			: 0.0f;
		const glm::vec2 closest = segA + segment * t;
		return glm::length(point - closest);
	}

	namespace
	{
		float Cross2D(glm::vec2 a, glm::vec2 b)
		{
			return a.x * b.y - a.y * b.x;
		}
	} // namespace

	bool PointInTriangle(glm::vec2 point, glm::vec2 a, glm::vec2 b, glm::vec2 c)
	{
		const float d1 = Cross2D(b - a, point - a);
		const float d2 = Cross2D(c - b, point - b);
		const float d3 = Cross2D(a - c, point - c);
		const bool hasNeg = (d1 < 0.0f) || (d2 < 0.0f) || (d3 < 0.0f);
		const bool hasPos = (d1 > 0.0f) || (d2 > 0.0f) || (d3 > 0.0f);
		return !(hasNeg && hasPos);
	}

	float DistancePointToTriangle2D(glm::vec2 point, glm::vec2 a, glm::vec2 b, glm::vec2 c)
	{
		if (PointInTriangle(point, a, b, c))
			return 0.0f;
		return std::min({
			DistancePointToSegment(point, a, b),
			DistancePointToSegment(point, b, c),
			DistancePointToSegment(point, c, a),
		});
	}

	void ClosestPointsRaySegment(
		const glm::vec3 &rayOrigin, const glm::vec3 &rayDir, const glm::vec3 &segA, const glm::vec3 &segB,
		float &outT, glm::vec3 &outClosestOnSegment)
	{
		const glm::vec3 d2 = segB - segA;
		const glm::vec3 w0 = rayOrigin - segA;
		const float e = glm::dot(d2, d2);
		if (e <= 1.0e-8f)
		{
			// Degenerate (zero-length) segment - treat segA as a point.
			outT = glm::dot(segA - rayOrigin, rayDir);
			outClosestOnSegment = segA;
			return;
		}

		const float b = glm::dot(rayDir, d2);
		const float c = glm::dot(rayDir, w0);
		const float f = glm::dot(d2, w0);
		const float denom = e - b * b; // a == dot(rayDir, rayDir) == 1 (normalized)

		// s is the segment parameter here (t is the ray's) - minimizing |w0 + t*rayDir - s*d2|^2 over
		// both gives t = b*s - c (from d/dt = 0) and s = (f - b*c)/denom (substituting that into
		// d/ds = 0). See SelectionHitTestTests.cpp's AsymmetricGeometry case for a worked example of
		// what goes wrong if this is instead computed as (b*f - c*e)/denom - that's the closed-form
		// RAY parameter from a two-segment solve (Ericson's ClosestPtSegmentSegment), not this
		// function's SEGMENT parameter, and only agrees with the correct formula in degenerate cases
		// (b==0 or e==1) - wrong for essentially every real bond (arbitrary length, arbitrary camera
		// angle).
		float s = denom > 1.0e-8f ? (f - b * c) / denom : 0.0f;
		s = std::clamp(s, 0.0f, 1.0f);
		outT = b * s - c;
		outClosestOnSegment = segA + d2 * s;
	}
} // namespace DefectStudio::SelectionHitTest
