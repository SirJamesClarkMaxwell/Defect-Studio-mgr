#include "Core/dspch.hpp"

#include <gtest/gtest.h>

#include <vector>

#include "Presentation/Panels/SceneOutlinerVisibilityColumns.hpp"

namespace DefectStudio::Tests
{
	namespace
	{
		struct VisibleObject
		{
			bool visible = true;
			bool renderable = true;
		};
	} // namespace

	TEST(SceneOutlinerVisibilityColumnsTests, MixedChildrenProduceIndependentMixedColumns)
	{
		const std::vector<VisibleObject> objects = {
			{true, true}, {false, true}, {true, false}};

		const SceneVisibilityColumnState state = SceneVisibilityStateFor(objects);

		EXPECT_TRUE(state.visibleMixed);
		EXPECT_TRUE(state.renderableMixed);
	}

	TEST(SceneOutlinerVisibilityColumnsTests, GroupEditChangesOnlyTheClickedColumn)
	{
		std::vector<VisibleObject> objects = {{false, false}, {true, false}};
		SceneVisibilityColumnEdit edit;
		edit.visibleChanged = true;
		edit.visible = true;

		ApplySceneVisibilityColumnEdit(objects, edit);

		EXPECT_TRUE(objects[0].visible);
		EXPECT_TRUE(objects[1].visible);
		EXPECT_FALSE(objects[0].renderable);
		EXPECT_FALSE(objects[1].renderable);
	}
} // namespace DefectStudio::Tests
