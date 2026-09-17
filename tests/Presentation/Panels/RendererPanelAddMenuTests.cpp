#include <gtest/gtest.h>

#include "Presentation/Panels/RendererPanelOrbitalMenu.hpp"

namespace DefectStudio::Tests
{
	TEST(DrawSelectionDescriptionTests, TwoAtomsEnableEveryDrawItemAndReportTheCount)
	{
		const DrawSelectionDescription description = DescribeDrawSelection(2);

		EXPECT_EQ(description.menuLabel, "Rysuj (2 atomy)");
		EXPECT_TRUE(description.canDrawSegment);
		EXPECT_TRUE(description.canDrawPlane);
		EXPECT_EQ(description.lineLabel, "Linia");
		EXPECT_EQ(description.arrowLabel, "Strzalka");
		EXPECT_EQ(description.planeLabel, "Plaszczyzna");
		EXPECT_TRUE(description.segmentTooltip.empty());
		EXPECT_TRUE(description.planeTooltip.empty());
	}

	TEST(DrawSelectionDescriptionTests, OneAtomKeepsTheMenuOpenAndExplainsDisabledItems)
	{
		const DrawSelectionDescription description = DescribeDrawSelection(1);

		EXPECT_EQ(description.menuLabel, "Rysuj (1 atom)");
		EXPECT_FALSE(description.canDrawSegment);
		EXPECT_FALSE(description.canDrawPlane);
		EXPECT_EQ(description.lineLabel, "Linia (wymaga dokladnie 2 atomow)");
		EXPECT_EQ(description.arrowLabel, "Strzalka (wymaga dokladnie 2 atomow)");
		EXPECT_EQ(description.planeLabel, "Plaszczyzna (wymaga co najmniej 2 atomow)");
		EXPECT_EQ(description.segmentTooltip, "Zaznacz dokladnie dwa atomy, aby dodac linie lub strzalke.");
		EXPECT_EQ(description.planeTooltip, "Zaznacz co najmniej dwa atomy, aby dodac plaszczyzne.");
	}

	TEST(DrawSelectionDescriptionTests, ThreeAtomsEnableOnlyThePlane)
	{
		const DrawSelectionDescription description = DescribeDrawSelection(3);

		EXPECT_EQ(description.menuLabel, "Rysuj (3 atomy)");
		EXPECT_FALSE(description.canDrawSegment);
		EXPECT_TRUE(description.canDrawPlane);
		EXPECT_NE(description.lineLabel.find("dokladnie 2"), std::string::npos);
		EXPECT_EQ(description.planeLabel, "Plaszczyzna");
	}

	TEST(DrawSelectionDescriptionTests, PolishCountSuffixIsSelfDescribingBeyondFour)
	{
		EXPECT_EQ(DescribeDrawSelection(0).menuLabel, "Rysuj (0 atomow)");
		EXPECT_EQ(DescribeDrawSelection(4).menuLabel, "Rysuj (4 atomy)");
		EXPECT_EQ(DescribeDrawSelection(5).menuLabel, "Rysuj (5 atomow)");
	}
} // namespace DefectStudio::Tests
