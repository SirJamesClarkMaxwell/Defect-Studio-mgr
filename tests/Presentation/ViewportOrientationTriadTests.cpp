#include "Core/dspch.hpp"

#include <gtest/gtest.h>

#include "Presentation/Panels/ViewportOrientationTriad.hpp"

namespace DefectStudio::Tests
{
	TEST(ViewportOrientationTriadTests, ResolveNormalizedOrientationAxesUsesLatticeColumnsWithoutLengthScaling)
	{
		TransformBases bases;
		glm::mat3 lattice(1.0f);
		lattice[0] = glm::vec3(2.0f, 0.0f, 0.0f);
		lattice[1] = glm::vec3(0.0f, 3.0f, 4.0f);
		lattice[2] = glm::vec3(0.0f, 0.0f, -7.0f);
		bases.lattice = lattice;

		const OrientationAxes axes = ResolveNormalizedOrientationAxes(TransformOrientation::Lattice, bases);

		EXPECT_EQ(axes[0], glm::vec3(1.0f, 0.0f, 0.0f));
		EXPECT_EQ(axes[1], glm::vec3(0.0f, 0.6f, 0.8f));
		EXPECT_EQ(axes[2], glm::vec3(0.0f, 0.0f, -1.0f));
	}

	TEST(ViewportOrientationTriadTests, ProjectOrientationAxesUsesCameraRotationOnly)
	{
		glm::mat4 translatedView(1.0f);
		translatedView[3] = glm::vec4(12.0f, -8.0f, 25.0f, 1.0f);
		const OrientationAxes axes = {
			glm::vec3(1.0f, 0.0f, 0.0f),
			glm::vec3(0.0f, 1.0f, 0.0f),
			glm::vec3(0.0f, 0.0f, 1.0f)};

		const OrientationScreenAxes projected = ProjectOrientationAxes(translatedView, axes);

		EXPECT_EQ(projected[0], glm::vec2(1.0f, 0.0f));
		EXPECT_EQ(projected[1], glm::vec2(0.0f, -1.0f));
		EXPECT_EQ(projected[2], glm::vec2(0.0f, 0.0f));
	}
} // namespace DefectStudio::Tests
