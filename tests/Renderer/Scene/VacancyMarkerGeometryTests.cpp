#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>

#include <glm/gtc/constants.hpp>

#include "Renderer/Scene/VacancyMarkerGeometry.hpp"

namespace DefectStudio::Tests
{
	namespace
	{
		const glm::vec3 kRight(1.0f, 0.0f, 0.0f);
		const glm::vec3 kUp(0.0f, 1.0f, 0.0f);
		const glm::vec3 kCentre(1.0f, 2.0f, 3.0f);

		[[nodiscard]] RendererVacancyData Marker(VacancyRenderMode mode, int dashCount)
		{
			RendererVacancyData vacancy;
			vacancy.cartesianPosition = kCentre;
			vacancy.radius = 0.5f;
			vacancy.renderMode = mode;
			vacancy.dashCount = dashCount;
			return vacancy;
		}

		[[nodiscard]] float AngleFromRight(const glm::vec3 &position)
		{
			const glm::vec3 offset = position - kCentre;
			float angle = std::atan2(glm::dot(offset, kUp), glm::dot(offset, kRight));
			if (angle < 0.0f)
				angle += glm::two_pi<float>();
			return angle;
		}
	} // namespace

	TEST(VacancyMarkerGeometryTests, GhostHasAFanFillAndADashedRing)
	{
		const VacancyMarkerMesh mesh =
			BuildVacancyMarkerMesh(Marker(VacancyRenderMode::Ghost, 12), kRight, kUp, 0.05f);

		EXPECT_EQ(mesh.fill.size(), static_cast<std::size_t>(kVacancyDiscSegments * 3));
		EXPECT_EQ(mesh.ring.size(), static_cast<std::size_t>(12 * kVacancyDashSubdivisions * 6));
	}

	TEST(VacancyMarkerGeometryTests, WireframeIsRingOnly)
	{
		const VacancyMarkerMesh mesh =
			BuildVacancyMarkerMesh(Marker(VacancyRenderMode::Wireframe, 8), kRight, kUp, 0.05f);

		EXPECT_TRUE(mesh.fill.empty());
		EXPECT_EQ(mesh.ring.size(), static_cast<std::size_t>(8 * kVacancyDashSubdivisions * 6));
	}

	TEST(VacancyMarkerGeometryTests, ZeroDashesIsAContinuousRing)
	{
		const VacancyMarkerMesh mesh =
			BuildVacancyMarkerMesh(Marker(VacancyRenderMode::Solid, 0), kRight, kUp, 0.05f);

		EXPECT_EQ(mesh.fill.size(), static_cast<std::size_t>(kVacancyDiscSegments * 3));
		EXPECT_EQ(mesh.ring.size(), static_cast<std::size_t>(kVacancySolidRingSegments * 6));
	}

	TEST(VacancyMarkerGeometryTests, DashCountIsClamped)
	{
		const VacancyMarkerMesh many =
			BuildVacancyMarkerMesh(Marker(VacancyRenderMode::Wireframe, 1000), kRight, kUp, 0.05f);
		const VacancyMarkerMesh negative =
			BuildVacancyMarkerMesh(Marker(VacancyRenderMode::Wireframe, -3), kRight, kUp, 0.05f);

		EXPECT_EQ(
			many.ring.size(), static_cast<std::size_t>(kVacancyMaxDashCount * kVacancyDashSubdivisions * 6));
		EXPECT_EQ(negative.ring.size(), static_cast<std::size_t>(kVacancySolidRingSegments * 6));
	}

	TEST(VacancyMarkerGeometryTests, EverythingLiesInTheCameraPlaneAndFacesTheViewer)
	{
		const VacancyMarkerMesh mesh =
			BuildVacancyMarkerMesh(Marker(VacancyRenderMode::Ghost, 12), kRight, kUp, 0.05f);
		const glm::vec3 facing(0.0f, 0.0f, 1.0f);

		for (const std::vector<IsosurfaceVertex> *soup : {&mesh.fill, &mesh.ring})
		{
			for (const IsosurfaceVertex &vertex : *soup)
			{
				EXPECT_NEAR(glm::dot(vertex.position - kCentre, facing), 0.0f, 1e-5f);
				EXPECT_NEAR(glm::length(vertex.normal - facing), 0.0f, 1e-5f);
			}
		}
		for (const IsosurfaceVertex &vertex : mesh.fill)
			EXPECT_FLOAT_EQ(vertex.sign, 1.0f);
		for (const IsosurfaceVertex &vertex : mesh.ring)
			EXPECT_FLOAT_EQ(vertex.sign, -1.0f);
	}

	TEST(VacancyMarkerGeometryTests, RingIsAnAnnulusOnTheRimAndFillEndsUnderIt)
	{
		const float width = 0.1f;
		const VacancyMarkerMesh mesh =
			BuildVacancyMarkerMesh(Marker(VacancyRenderMode::Ghost, 12), kRight, kUp, width);

		for (const IsosurfaceVertex &vertex : mesh.ring)
		{
			const float distance = glm::length(vertex.position - kCentre);
			EXPECT_GE(distance, 0.5f - width - 1e-4f);
			EXPECT_LE(distance, 0.5f + 1e-4f);
		}
		float fillMax = 0.0f;
		for (const IsosurfaceVertex &vertex : mesh.fill)
			fillMax = std::max(fillMax, glm::length(vertex.position - kCentre));
		EXPECT_NEAR(fillMax, 0.5f - 0.5f * width, 1e-4f);
	}

	// Dash k covers [k, k + 0.5] of a period, so no ring vertex lies strictly inside the second half
	// of any period - which is what makes the ring read as dashed.
	TEST(VacancyMarkerGeometryTests, DashesLeaveGapsOfHalfAPeriod)
	{
		const int dashes = 6;
		const VacancyMarkerMesh mesh =
			BuildVacancyMarkerMesh(Marker(VacancyRenderMode::Wireframe, dashes), kRight, kUp, 0.05f);
		const float period = glm::two_pi<float>() / static_cast<float>(dashes);

		for (const IsosurfaceVertex &vertex : mesh.ring)
		{
			const float phase = std::fmod(AngleFromRight(vertex.position), period) / period;
			const bool inDash = phase <= 0.5f + 1e-3f || phase >= 1.0f - 1e-3f;
			EXPECT_TRUE(inDash) << "phase " << phase;
		}
	}

	TEST(VacancyMarkerGeometryTests, RingWidthIsCappedAtTheRadius)
	{
		const VacancyMarkerMesh mesh =
			BuildVacancyMarkerMesh(Marker(VacancyRenderMode::Wireframe, 4), kRight, kUp, 5.0f);

		ASSERT_FALSE(mesh.ring.empty());
		for (const IsosurfaceVertex &vertex : mesh.ring)
			EXPECT_LE(glm::length(vertex.position - kCentre), 0.5f + 1e-4f);
	}

	TEST(VacancyMarkerGeometryTests, DegenerateInputsGiveNothing)
	{
		RendererVacancyData zeroRadius = Marker(VacancyRenderMode::Ghost, 12);
		zeroRadius.radius = 0.0f;

		const VacancyMarkerMesh noRadius = BuildVacancyMarkerMesh(zeroRadius, kRight, kUp, 0.05f);
		const VacancyMarkerMesh noWidth =
			BuildVacancyMarkerMesh(Marker(VacancyRenderMode::Ghost, 12), kRight, kUp, 0.0f);

		EXPECT_TRUE(noRadius.fill.empty());
		EXPECT_TRUE(noRadius.ring.empty());
		EXPECT_TRUE(noWidth.fill.empty());
		EXPECT_TRUE(noWidth.ring.empty());
	}
} // namespace DefectStudio::Tests
