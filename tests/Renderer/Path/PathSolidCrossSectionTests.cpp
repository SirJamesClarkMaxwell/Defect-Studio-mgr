#include "Core/dspch.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <vector>

#include <gtest/gtest.h>

#include "Renderer/Path/PathDecorationMesher.hpp"
#include "Renderer/Path/PathStrokeMesher.hpp"

namespace DefectStudio::Tests
{
	namespace
	{
		constexpr double kTolerance = 1.0e-5;

		EvaluatedPath StraightPath()
		{
			EvaluatedPath path;
			path.totalLength = 2.0;
			path.samples = {
				{{0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {0.0, 0.0, 1.0}, {}, 0.0, 0.0, 0.0},
				{{2.0, 0.0, 0.0}, {1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {0.0, 0.0, 1.0}, {}, 1.0, 2.0, 1.0}};
			return path;
		}

		struct SectionFace
		{
			glm::dvec3 position{0.0};
			glm::dvec3 normal{0.0};
		};

		// Intersect triangles with an interior sample plane. End bevels have extra vertices and
		// faces, so neither array cardinality nor vertex order describes the shaft cross-section.
		std::vector<SectionFace> CrossSection(const StrokeGeometry &geometry, const EvaluatedPath &path)
		{
			const glm::dvec3 centre = (path.samples.front().position + path.samples.back().position) * 0.5;
			const glm::dvec3 axis = path.samples.front().tangent;
			std::vector<SectionFace> result;
			for (std::size_t index = geometry.shaft.firstIndex;
				index + 2u < geometry.shaft.firstIndex + geometry.shaft.indexCount; index += 3u)
			{
				std::array<glm::dvec3, 3u> positions;
				std::array<glm::dvec3, 3u> normals;
				for (std::size_t corner = 0u; corner < 3u; ++corner)
				{
					const std::uint32_t vertex = geometry.indices[index + corner];
					EXPECT_LT(vertex, geometry.tubeVertices.size());
					if (vertex >= geometry.tubeVertices.size())
						return {};
					positions[corner] = geometry.tubeVertices[vertex].position;
					normals[corner] = geometry.tubeVertices[vertex].normal;
				}
				std::vector<glm::dvec3> intersections;
				const auto append = [&](const glm::dvec3 &point) {
					if (std::none_of(intersections.begin(), intersections.end(), [&](const glm::dvec3 &other) {
						return glm::distance(point, other) <= kTolerance;
					}))
						intersections.push_back(point);
				};
				for (std::size_t edge = 0u; edge < 3u; ++edge)
				{
					const glm::dvec3 &first = positions[edge];
					const glm::dvec3 &second = positions[(edge + 1u) % 3u];
					const double a = glm::dot(first - centre, axis);
					const double b = glm::dot(second - centre, axis);
					if (std::abs(a) <= kTolerance)
						append(first);
					if ((a < -kTolerance && b > kTolerance) || (a > kTolerance && b < -kTolerance))
						append(first + (second - first) * (a / (a - b)));
				}
				if (intersections.size() != 2u || glm::distance(intersections[0], intersections[1]) <= kTolerance)
					continue;
				const glm::dvec3 geometricNormal = glm::normalize(glm::cross(
					positions[1] - positions[0], positions[2] - positions[0]));
				for (const glm::dvec3 &normal : normals)
				{
					EXPECT_NEAR(glm::length(normal), 1.0, kTolerance);
					EXPECT_GT(glm::dot(normal, normals[0]), 1.0 - kTolerance);
					EXPECT_GT(glm::dot(normal, geometricNormal), 1.0 - kTolerance);
				}
				result.push_back({(intersections[0] + intersections[1]) * 0.5 - centre, normals[0]});
			}
			return result;
		}

		std::vector<glm::dvec3> DistinctNormals(const std::vector<SectionFace> &section)
		{
			std::vector<glm::dvec3> result;
			for (const SectionFace &face : section)
				if (std::none_of(result.begin(), result.end(), [&](const glm::dvec3 &normal) {
					return glm::dot(normal, face.normal) > 1.0 - kTolerance;
				}))
					result.push_back(face.normal);
			return result;
		}

		std::vector<glm::dvec3> SolidFaceNormals(const StrokeGeometry &geometry)
		{
			std::vector<SectionFace> faces;
			for (std::size_t index = 0u; index + 2u < geometry.indices.size(); index += 3u)
			{
				std::array<glm::dvec3, 3u> positions;
				std::array<glm::dvec3, 3u> normals;
				for (std::size_t corner = 0u; corner < 3u; ++corner)
				{
					const std::uint32_t vertex = geometry.indices[index + corner];
					EXPECT_LT(vertex, geometry.tubeVertices.size());
					if (vertex >= geometry.tubeVertices.size())
						return {};
					positions[corner] = geometry.tubeVertices[vertex].position;
					normals[corner] = geometry.tubeVertices[vertex].normal;
				}
				const glm::dvec3 geometricNormal = glm::normalize(glm::cross(
					positions[1] - positions[0], positions[2] - positions[0]));
				for (const glm::dvec3 &normal : normals)
				{
					EXPECT_NEAR(glm::length(normal), 1.0, kTolerance);
					EXPECT_GT(glm::dot(normal, normals[0]), 1.0 - kTolerance);
					EXPECT_GT(glm::dot(normal, geometricNormal), 1.0 - kTolerance);
				}
				faces.push_back({{}, normals[0]});
			}
			return DistinctNormals(faces);
		}
	} // namespace

	TEST(PathStrokeMesherTests, FlatRibbonBevelAddsChamferFacesWithConstantFrameNormals)
	{
		const EvaluatedPath path = StraightPath();
		PathStrokeStyle sharpStyle;
		sharpStyle.profile = StrokeProfile::Flat;
		sharpStyle.width = 0.4f;
		sharpStyle.ribbonThickness = 0.6f;
		const StrokeGeometry sharp = BuildStroke(path, sharpStyle);
		PathStrokeStyle bevelStyle = sharpStyle;
		bevelStyle.ribbonBevel = 0.1f;
		const StrokeGeometry bevel = BuildStroke(path, bevelStyle);
		const auto sharpNormals = SolidFaceNormals(sharp);
		const auto bevelNormals = SolidFaceNormals(bevel);
		ASSERT_EQ(sharpNormals.size(), 6u);
		EXPECT_GT(bevelNormals.size(), sharpNormals.size());

		const glm::dvec3 widthAxis = path.samples.front().normal;
		const glm::dvec3 depthAxis = path.samples.front().binormal;
		const glm::dvec3 tangent = path.samples.front().tangent;
		const std::array axes = {widthAxis, depthAxis, tangent};
		for (const glm::dvec3 flat : {widthAxis, -widthAxis, depthAxis, -depthAxis, tangent, -tangent})
			EXPECT_TRUE(std::any_of(bevelNormals.begin(), bevelNormals.end(), [&](const glm::dvec3 &normal) {
				return glm::dot(normal, flat) > 1.0 - kTolerance;
			}));
		std::size_t chamfers = 0u;
		for (const glm::dvec3 &normal : bevelNormals)
		{
			const auto active = std::count_if(axes.begin(), axes.end(), [&](const glm::dvec3 &axis) {
				return std::abs(glm::dot(normal, axis)) > kTolerance;
			});
			// Corner patches meet three faces; edge chamfers meet exactly two. Check end
			// chamfers as well as the four long shaft edges.
			if (active != 2)
				continue;
			++chamfers;
			for (const glm::dvec3 &axis : axes)
				if (std::abs(glm::dot(normal, axis)) > kTolerance)
				{
					const glm::dvec3 neighbour = axis * (glm::dot(normal, axis) > 0.0 ? 1.0 : -1.0);
					EXPECT_GT(glm::dot(normal, neighbour), kTolerance);
					EXPECT_LT(glm::dot(normal, neighbour), 1.0 - kTolerance);
				}
		}
		EXPECT_EQ(chamfers, 12u);

		PathStrokeStyle cappedStyle = sharpStyle;
		cappedStyle.ribbonBevel = 0.5f * std::min(sharpStyle.width, sharpStyle.ribbonThickness);
		PathStrokeStyle overStyle = sharpStyle;
		overStyle.ribbonBevel = cappedStyle.ribbonBevel * 4.0f;
		const StrokeGeometry capped = BuildStroke(path, cappedStyle);
		const StrokeGeometry over = BuildStroke(path, overStyle);
		ASSERT_EQ(capped.indices, over.indices);
		ASSERT_EQ(capped.tubeVertices.size(), over.tubeVertices.size());
		ASSERT_EQ(capped.ribbonVertices.size(), over.ribbonVertices.size());
		for (std::size_t index = 0u; index < capped.tubeVertices.size(); ++index)
			EXPECT_EQ(capped.tubeVertices[index].position, over.tubeVertices[index].position);
	}

	TEST(PathStrokeMesherTests, EmittedBevelRingCardinalityMatchesCrossSectionRingSize)
	{
		const EvaluatedPath path = StraightPath();
		for (const std::uint32_t segments : {1u, 4u, 16u})
		{
			PathStrokeStyle style;
			style.profile = StrokeProfile::Flat;
			style.width = 0.4f;
			style.ribbonThickness = 0.6f;
			style.ribbonBevel = 0.1f;
			style.ribbonBevelSegments = segments;
			SCOPED_TRACE(segments);
			const auto normals = DistinctNormals(CrossSection(BuildStroke(path, style), path));
			// A faceted ring carries two vertices per face. Count the faces of the solid's
			// interior section, without including its end chamfers or corner patches.
			EXPECT_EQ(normals.size(), detail::CrossSectionRingSize(style) / 2u);
		}
	}

	TEST(PathStrokeMesherTests, HighSegmentBevelProfilesNeverDoubleBack)
	{
		const EvaluatedPath path = StraightPath();
		const glm::dvec3 widthAxis = path.samples.front().normal;
		const glm::dvec3 depthAxis = path.samples.front().binormal;
		for (const float shape : {0.0f, 0.5f, 1.0f})
		{
			PathStrokeStyle style;
			style.profile = StrokeProfile::Flat;
			style.width = 0.4f;
			style.ribbonThickness = 0.6f;
			style.ribbonBevel = 0.1f;
			style.ribbonBevelSegments = 16u;
			style.ribbonBevelShape = shape;
			const auto section = CrossSection(BuildStroke(path, style), path);
			for (const double widthSign : {-1.0, 1.0})
				for (const double depthSign : {-1.0, 1.0})
				{
					SCOPED_TRACE(::testing::Message() << "shape=" << shape << ", corner=" << widthSign << "," << depthSign);
					const glm::dvec3 widthNormal = widthAxis * widthSign;
					const glm::dvec3 depthNormal = depthAxis * depthSign;
					std::vector<SectionFace> profile;
					for (const SectionFace &face : section)
						if (glm::dot(face.position, widthNormal) >= style.width * 0.5 - style.ribbonBevel - kTolerance &&
							glm::dot(face.position, depthNormal) >= style.ribbonThickness * 0.5 - style.ribbonBevel - kTolerance)
							profile.push_back(face);
					ASSERT_GE(profile.size(), style.ribbonBevelSegments);
					const auto positionAngle = [&](const SectionFace &face) {
						return std::atan2(glm::dot(face.position, depthNormal), glm::dot(face.position, widthNormal));
					};
					std::sort(profile.begin(), profile.end(), [&](const SectionFace &a, const SectionFace &b) {
						return positionAngle(a) > positionAngle(b);
					});
					// Concave profiles turn in the opposite direction to convex profiles. Both must
					// stay between the neighbouring flat normals and never reverse their turn.
					const double turnSign = shape < 0.5f ? 1.0 : -1.0;
					double previousAngle = 0.0;
					const glm::dvec3 direction = profile.back().position - profile.front().position;
					ASSERT_GT(glm::dot(direction, direction), kTolerance * kTolerance);
					double previousProgress = -kTolerance;
					for (std::size_t index = 0u; index < profile.size(); ++index)
					{
						const SectionFace &face = profile[index];
						const double angle = std::atan2(glm::dot(face.normal, depthNormal), glm::dot(face.normal, widthNormal));
						EXPECT_GE(angle, -kTolerance);
						EXPECT_LE(angle, std::numbers::pi * 0.5 + kTolerance);
						if (index > 0u)
							EXPECT_GE(turnSign * (angle - previousAngle), -kTolerance);
						previousAngle = angle;
						const double progress = glm::dot(face.position - profile.front().position, direction);
						EXPECT_GE(progress, previousProgress - kTolerance);
						previousProgress = progress;
					}
					const double firstAngle = std::atan2(glm::dot(profile.front().normal, depthNormal), glm::dot(profile.front().normal, widthNormal));
					EXPECT_GT(turnSign * (previousAngle - firstAngle), 1.0);
				}
		}
	}
}
