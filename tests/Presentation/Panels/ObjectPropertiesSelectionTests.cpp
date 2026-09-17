#include <gtest/gtest.h>

#include "Presentation/Panels/ObjectPropertiesSelection.hpp"

namespace DefectStudio::Tests
{
	TEST(ObjectPropertiesSelectionTests, NoSupportedSelectionNeedsTheEmptyMessage)
	{
		const RendererWindowState window;

		const ObjectPropertiesSections sections = ResolveObjectPropertiesSections(window);

		EXPECT_TRUE(sections.Empty());
	}

	TEST(ObjectPropertiesSelectionTests, EverySelectedKindGetsItsOwnSection)
	{
		RendererWindowState window;
		window.selectedAtomIndices = {1, 2};
		window.selectedPinnedMeasurements = {SceneObjectId{11}};
		window.selectedFreeLabels = {SceneObjectId{12}};
		window.selectedSceneArrows = {SceneObjectId{13}};
		window.selectedSceneOrbitals = {SceneObjectId{14}};
		window.selectedScenePlanes = {SceneObjectId{15}};

		const ObjectPropertiesSections sections = ResolveObjectPropertiesSections(window);

		EXPECT_TRUE(sections.atoms);
		EXPECT_TRUE(sections.labels);
		EXPECT_TRUE(sections.arrows);
		EXPECT_TRUE(sections.orbitals);
		EXPECT_TRUE(sections.planes);
		EXPECT_FALSE(sections.Empty());
	}

	TEST(ObjectPropertiesSelectionTests, LabelsCombinePinnedAndFreeSelections)
	{
		RendererWindowState pinnedWindow;
		pinnedWindow.selectedPinnedMeasurements = {SceneObjectId{21}};
		RendererWindowState freeWindow;
		freeWindow.selectedFreeLabels = {SceneObjectId{22}};

		EXPECT_TRUE(ResolveObjectPropertiesSections(pinnedWindow).labels);
		EXPECT_TRUE(ResolveObjectPropertiesSections(freeWindow).labels);
	}
} // namespace DefectStudio::Tests
