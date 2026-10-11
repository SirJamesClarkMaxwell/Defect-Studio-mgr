#include <gtest/gtest.h>

#include "Presentation/Panels/PlotWidgets.hpp"

namespace DefectStudio::Tests
{
	TEST(PlotWidgetsTests, TexLiteSplitsSubscriptsSuperscriptsAndSymbols)
	{
		const auto runs = ParseTexLite("E_F - E_{VBM} [eV], c [cm^{-3}], \\Delta\\mu_{Ge}");
		ASSERT_EQ(runs.size(), 8u);
		EXPECT_EQ(runs[0].text, "E");
		EXPECT_EQ(runs[1].text, "F");
		EXPECT_EQ(runs[1].level, -1);
		EXPECT_EQ(runs[2].text, " - E");
		EXPECT_EQ(runs[3].text, "VBM");
		EXPECT_EQ(runs[4].text, " [eV], c [cm");
		EXPECT_EQ(runs[5].text, "-3");
		EXPECT_EQ(runs[5].level, 1);
		EXPECT_EQ(runs[6].text, "], Δμ");
		EXPECT_EQ(runs[7].text, "Ge");
		EXPECT_EQ(runs[7].level, -1);
	}
} // namespace DefectStudio::Tests
