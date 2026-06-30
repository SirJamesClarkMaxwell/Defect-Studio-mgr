#include <gtest/gtest.h>

#include "Renderer/RendererStartupBootstrap.hpp"

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
} // namespace DefectStudio::Tests
