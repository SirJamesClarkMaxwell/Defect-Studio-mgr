#include <gtest/gtest.h>

#include "Renderer/Scene/SceneObjectAppearance.hpp"

namespace DefectStudio::Tests
{
	TEST(SceneObjectSelectionColorTests, SelectionBlendsEveryColorTowardTheSharedAccent)
	{
		const glm::vec3 base(0.1f, 0.2f, 0.3f);

		const glm::vec3 unchanged = ApplySceneSelectionHighlight(base, false);
		const glm::vec3 selected = ApplySceneSelectionHighlight(base, true);
		const glm::vec3 expected = glm::mix(base, SceneSelectionHighlightColor(), 0.55f);

		EXPECT_FLOAT_EQ(unchanged.r, base.r);
		EXPECT_FLOAT_EQ(unchanged.g, base.g);
		EXPECT_FLOAT_EQ(unchanged.b, base.b);
		EXPECT_FLOAT_EQ(selected.r, expected.r);
		EXPECT_FLOAT_EQ(selected.g, expected.g);
		EXPECT_FLOAT_EQ(selected.b, expected.b);
	}

} // namespace DefectStudio::Tests
