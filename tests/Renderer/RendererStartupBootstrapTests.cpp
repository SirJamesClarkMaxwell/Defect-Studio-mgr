#include <gtest/gtest.h>

#include <glm/geometric.hpp>

#include "Renderer/RendererStartupBootstrap.hpp"
#include "Renderer/RendererViewCamera.hpp"

namespace DefectStudio::Tests
{
	TEST(RendererStartupBootstrapTests, BuildsWindowsFromPreparedRendererStructures)
	{
		RendererStructureData structure;
		structure.name = "Prepared";
		structure.atoms = {
			RendererAtomData{"C", glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(1.0f), 0.4f, true},
			RendererAtomData{"C", glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(1.0f), 0.4f, true},
			RendererAtomData{"C", glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(1.0f), 0.4f, true}};

		RendererStartupWindowDefinition definition;
		definition.title = "Prepared window";
		definition.structureName = "Prepared";
		definition.direction = glm::vec3(0.0f, 0.0f, 1.0f);
		definition.distanceMultiplier = 1.5f;
		definition.hideStep = 2;

		RendererStartupWindowInput input;
		input.definition = definition;
		input.structure = std::move(structure);

		std::vector<RendererStartupWindowInput> inputs;
		inputs.push_back(std::move(input));
		std::vector<RendererWindowState> windows = BuildRendererStartupWindows(std::move(inputs));

		ASSERT_EQ(windows.size(), 1u);
		EXPECT_EQ(windows[0].title, "Prepared window");
		EXPECT_EQ(windows[0].structure.name, "Prepared");
		ASSERT_NE(windows[0].camera, nullptr);
		ASSERT_EQ(windows[0].structure.atoms.size(), 3u);
		EXPECT_FALSE(windows[0].structure.atoms[0].visible);
		EXPECT_TRUE(windows[0].structure.atoms[1].visible);
		EXPECT_FALSE(windows[0].structure.atoms[2].visible);
	}

	// The "+" menu-bar button opens a window with no atoms at all. Without the empty guard in
	// BuildWindowFromStructure the inverted sentinel bounds put the camera ~3.9e6 units out and the
	// grid is a single invisible speck.
	TEST(RendererStartupBootstrapTests, FramesAnEmptyStructureOnTheOriginInsteadOfMillionsOfUnitsOut)
	{
		RendererStartupWindowInput input;
		input.definition.title = "Pusta scena";
		input.definition.structureName = "Pusta scena";
		input.definition.direction = glm::vec3(1.0f, 1.0f, 1.0f);

		std::vector<RendererStartupWindowInput> inputs;
		inputs.push_back(std::move(input));
		std::vector<RendererWindowState> windows = BuildRendererStartupWindows(std::move(inputs));

		ASSERT_EQ(windows.size(), 1u);
		ASSERT_NE(windows[0].camera, nullptr);
		EXPECT_TRUE(windows[0].structure.atoms.empty());
		EXPECT_LT(windows[0].camera->Distance(), 100.0f);
		EXPECT_NEAR(glm::length(windows[0].camera->Target()), 0.0f, 1e-4f);
	}

	TEST(RendererViewCameraTests, TransitionDurationScalesWithRotationSpeed)
	{
		EXPECT_NEAR(RendererViewCamera::ComputeTransitionDurationSeconds(1.0f), 0.14f, 1e-5f);
		EXPECT_NEAR(RendererViewCamera::ComputeTransitionDurationSeconds(0.01f), 0.50f, 1e-5f);
		EXPECT_NEAR(RendererViewCamera::ComputeTransitionDurationSeconds(100.0f), 0.02f, 1e-5f);
	}
} // namespace DefectStudio::Tests
