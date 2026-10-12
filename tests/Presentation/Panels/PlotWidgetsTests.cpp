#include <gtest/gtest.h>
#include <limits>

#include "Presentation/Panels/PlotWidgets.hpp"

namespace DefectStudio::Tests
{
	TEST(PlotWidgetsTests, SeriesStyleUsesAutoOrExplicitRgbaAndPreservesCallerFlags)
	{
		ImPlotSpec spec;
		spec.Flags = ImPlotItemFlags_NoFit;
		PlotSeriesStyle style;
		ApplySeriesStyle(style, spec);
		EXPECT_FLOAT_EQ(spec.LineColor.w, IMPLOT_AUTO_COL.w);
		EXPECT_FLOAT_EQ(spec.LineWeight, 1.5f);
		style.color = glm::vec4(0.1f, 0.2f, 0.3f, 0.4f);
		style.lineWeight = 3.0f;
		style.fillAlpha = 0.7f;
		ApplySeriesStyle(style, spec);
		EXPECT_FLOAT_EQ(spec.LineColor.x, 0.1f);
		EXPECT_FLOAT_EQ(spec.LineColor.w, 0.4f);
		EXPECT_FLOAT_EQ(spec.FillColor.z, 0.3f);
		EXPECT_FLOAT_EQ(spec.LineWeight, 3.0f);
		EXPECT_FLOAT_EQ(spec.FillAlpha, 0.7f);
		EXPECT_EQ(spec.Flags, ImPlotItemFlags_NoFit);
	}

	TEST(PlotWidgetsTests, DashesContinueAcrossSamplesAndAreClipped)
	{
		const std::vector<glm::dvec2> points{{-5, 0}, {5, 0}, {25, 0}};
		const auto segments = PlotDashSegments(points, {0, -1, 20, 1}, 1);
		ASSERT_EQ(segments.size(), 6u);
		const double expected[] = {0, 3, 7, 15, 19, 20};
		for (std::size_t i = 0; i < segments.size(); ++i)
		{
			EXPECT_NEAR(segments[i].x, expected[i], 1e-9);
			EXPECT_DOUBLE_EQ(segments[i].y, 0.0);
		}
	}

	TEST(PlotWidgetsTests, DotsBreakAtNanAndIgnoreZeroLengthOrOutsideSegments)
	{
		const double nan = std::numeric_limits<double>::quiet_NaN();
		const std::vector<glm::dvec2> points{{0, 0}, {0, 0}, {12, 0}, {nan, nan}, {0, 2}, {12, 2}, {nan, nan}, {0, 0}, {6, 0}};
		const auto segments = PlotDashSegments(points, {0, -1, 12, 1}, 2);
		ASSERT_EQ(segments.size(), 6u);
		EXPECT_DOUBLE_EQ(segments[0].x, 0.0);
		EXPECT_DOUBLE_EQ(segments[1].x, 1.0);
		EXPECT_DOUBLE_EQ(segments[2].x, 6.0);
		EXPECT_DOUBLE_EQ(segments[3].x, 7.0);
		EXPECT_DOUBLE_EQ(segments[4].x, 0.0);
		EXPECT_DOUBLE_EQ(segments[5].x, 1.0);
	}

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
