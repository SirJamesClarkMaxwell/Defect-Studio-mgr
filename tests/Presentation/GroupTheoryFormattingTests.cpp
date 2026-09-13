#include <gtest/gtest.h>

#include "Presentation/Panels/GroupTheoryFormatting.hpp"

namespace
{
	using namespace DefectStudio;

	CharacterTable MakeC3vTable()
	{
		CharacterTable table;
		table.pointGroupLabel = "C3v";
		table.groupOrder = 6;
		table.classLabels = {"E", "2C3", "3sv"};
		table.classSizes = {1, 2, 3};
		table.irrepLabels = {"A1", "A2", "E"};
		table.irrepDimensions = {1, 1, 2};
		table.characters = {
			{{"1", 1.0}, {"1", 1.0}, {"1", 1.0}},
			{{"1", 1.0}, {"1", 1.0}, {"-1", -1.0}},
			{{"2", 2.0}, {"-1", -1.0}, {"0", 0.0}}};
		return table;
	}

	TEST(GroupTheoryFormattingTests, IrrepLabels)
	{
		EXPECT_EQ(FormatIrrepLabel("A1", LabelStyle::Unicode), "A\u2081");
		EXPECT_EQ(FormatIrrepLabel("T2g", LabelStyle::Unicode), "T\u2082g");
		EXPECT_EQ(FormatIrrepLabel("E", LabelStyle::Unicode), "E");
		EXPECT_EQ(FormatIrrepLabel("A'", LabelStyle::Unicode), "A'");
		EXPECT_EQ(FormatIrrepLabel("A1", LabelStyle::Latex), "A_{1}");
		EXPECT_EQ(FormatIrrepLabel("T2g", LabelStyle::Latex), "T_{2g}");
		EXPECT_EQ(FormatIrrepLabel("C3v", LabelStyle::Latex), "C_{3v}");
		EXPECT_EQ(FormatIrrepLabel("A1'", LabelStyle::Latex), "A_{1}'");
		EXPECT_EQ(FormatIrrepLabel("E", LabelStyle::Latex), "E");
	}

	TEST(GroupTheoryFormattingTests, ClassLabels)
	{
		EXPECT_EQ(FormatClassLabel("E", LabelStyle::Unicode), "E");
		EXPECT_EQ(FormatClassLabel("2C3", LabelStyle::Unicode), "2C\u2083");
		EXPECT_EQ(FormatClassLabel("3sv", LabelStyle::Unicode), "3\u03C3v");
		EXPECT_EQ(FormatClassLabel("i", LabelStyle::Unicode), "i");
		EXPECT_EQ(FormatClassLabel("3C2'", LabelStyle::Unicode), "3C\u2082'");
		EXPECT_EQ(FormatClassLabel("2C3", LabelStyle::Latex), "2C_{3}");
		EXPECT_EQ(FormatClassLabel("3sv", LabelStyle::Latex), "3\\sigma_{v}");
		EXPECT_EQ(FormatClassLabel("sh", LabelStyle::Latex), "\\sigma_{h}");
	}

	TEST(GroupTheoryFormattingTests, TermLabels)
	{
		EXPECT_EQ(FormatTermLabel(3, "A2", LabelStyle::Unicode), "\u00B3A\u2082");
		EXPECT_EQ(FormatTermLabel(1, "E", LabelStyle::Unicode), "\u00B9E");
		EXPECT_EQ(FormatTermLabel(3, "A2", LabelStyle::Latex), "^{3}A_{2}");
	}

	TEST(GroupTheoryFormattingTests, Decomposition)
	{
		const std::vector<IrrepMultiplicity> decomposition = {{"A1", 2, 1}, {"E", 1, 2}};
		EXPECT_EQ(FormatDecomposition(decomposition, LabelStyle::Unicode), "\u0393 = 2A\u2081 \u2295 E");
		EXPECT_EQ(FormatDecomposition(decomposition, LabelStyle::Latex), "\\Gamma = 2A_{1} \\oplus E");
		EXPECT_EQ(FormatDecomposition({}, LabelStyle::Unicode), "\u0393 = 0");
	}

	TEST(GroupTheoryFormattingTests, CharacterTableMarkdownWithReducibleRow)
	{
		const std::vector<ExactCoefficient> reducible = {{"4", 4.0}, {"1", 1.0}, {"2", 2.0}};
		EXPECT_EQ(
			FormatCharacterTable(MakeC3vTable(), reducible, TableFormat::Markdown),
			"| C\u2083v | E | 2C\u2083 | 3\u03C3v |\n"
			"|---|---|---|---|\n"
			"| A\u2081 | 1 | 1 | 1 |\n"
			"| A\u2082 | 1 | 1 | -1 |\n"
			"| E | 2 | -1 | 0 |\n"
			"| \u0393 | 4 | 1 | 2 |\n");
	}

	TEST(GroupTheoryFormattingTests, CharacterTableLatexWithoutReducibleRow)
	{
		EXPECT_EQ(
			FormatCharacterTable(MakeC3vTable(), {}, TableFormat::Latex),
			"\\begin{tabular}{l|ccc}\n"
			"$C_{3v}$ & $E$ & $2C_{3}$ & $3\\sigma_{v}$ \\\\\n"
			"\\hline\n"
			"$A_{1}$ & 1 & 1 & 1 \\\\\n"
			"$A_{2}$ & 1 & 1 & -1 \\\\\n"
			"$E$ & 2 & -1 & 0 \\\\\n"
			"\\end{tabular}\n");
	}

	TEST(GroupTheoryFormattingTests, Multiplets)
	{
		const std::vector<MultipletTerm> terms = {{"A2", 3, 1, 1, 3}, {"E", 1, 2, 1, 2}};
		EXPECT_EQ(
			FormatMultiplets(terms, TableFormat::Markdown),
			"| Term | Count per row | States |\n"
			"|---|---|---|\n"
			"| \u00B3A\u2082 | 1 | 3 |\n"
			"| \u00B9E | 1 | 2 |\n");
		EXPECT_EQ(
			FormatMultiplets(terms, TableFormat::Latex),
			"\\begin{tabular}{lcc}\n"
			"Term & Count per row & States \\\\\n"
			"\\hline\n"
			"$^{3}A_{2}$ & 1 & 3 \\\\\n"
			"$^{1}E$ & 1 & 2 \\\\\n"
			"\\end{tabular}\n");
	}

	TEST(GroupTheoryFormattingTests, TensorPower)
	{
		const std::vector<IrrepMultiplicity> decomposition = {{"A1", 715, 1}, {"A2", 651, 1}, {"E", 1365, 2}};
		EXPECT_EQ(
			FormatTensorPower(6, decomposition, LabelStyle::Unicode),
			"\u0393\u2297\u2076 = 715A\u2081 \u2295 651A\u2082 \u2295 1365E");
		EXPECT_EQ(
			FormatTensorPower(6, decomposition, LabelStyle::Latex),
			"\\Gamma^{\\otimes 6} = 715A_{1} \\oplus 651A_{2} \\oplus 1365E");
		EXPECT_EQ(FormatTensorPower(12, {}, LabelStyle::Unicode), "\u0393\u2297\u00B9\u00B2: dimension too large");
	}

	TEST(GroupTheoryFormattingTests, ActiveIrrepsFromDecomposition)
	{
		EXPECT_EQ(ActiveIrrepsFromDecomposition({{"A1", 2, 1}, {"E", 1, 2}}), "A1, A1, E");
		EXPECT_EQ(ActiveIrrepsFromDecomposition({}), "");
	}

	TEST(GroupTheoryFormattingTests, ExactValues)
	{
		EXPECT_EQ(FormatExactValue({"sqrt(3)/3", 0.577}, LabelStyle::Unicode), "\u221A3/3");
		EXPECT_EQ(FormatExactValue({"-sqrt(6)/6", -0.408}, LabelStyle::Unicode), "-\u221A6/6");
		EXPECT_EQ(FormatExactValue({"-1/2 + sqrt(3)*I/2", -0.5, 0.866}, LabelStyle::Unicode), "-1/2 + \u221A3i/2");
		EXPECT_EQ(FormatExactValue({"2*sqrt(2)", 2.828}, LabelStyle::Unicode), "2\u00B7\u221A2");
		EXPECT_EQ(FormatExactValue({"I", 0.0, 1.0}, LabelStyle::Unicode), "i");
		EXPECT_EQ(FormatExactValue({"0", 0.0}, LabelStyle::Unicode), "0");
		EXPECT_EQ(FormatExactValue({"sqrt(2)/2", 0.707, 0.0, "\\frac{\\sqrt{2}}{2}"}, LabelStyle::Latex), "\\frac{\\sqrt{2}}{2}");
		EXPECT_EQ(FormatExactValue({"-1", -1.0}, LabelStyle::Latex), "-1");
	}

	TEST(GroupTheoryFormattingTests, MathSegments)
	{
		const auto check = [](std::string_view latex, const std::vector<MathSegment> &expected) {
			const std::vector<MathSegment> actual = SplitMathSegments(latex);
			ASSERT_EQ(actual.size(), expected.size()) << latex;
			for (std::size_t i = 0; i < expected.size(); ++i)
			{
				EXPECT_EQ(actual[i].text, expected[i].text) << latex << " segment " << i;
				EXPECT_EQ(actual[i].level, expected[i].level) << latex << " segment " << i;
			}
		};
		check("E", {{"E", 0}});
		check("3\\sigma_{v}", {{"3\u03C3", 0}, {"v", -1}});
		check("^{3}A_{2}", {{"3", 1}, {"A", 0}, {"2", -1}});
		check("C_{3v}", {{"C", 0}, {"3v", -1}});
		check("A_{1}'", {{"A", 0}, {"1", -1}, {"'", 0}});
		check("\\Gamma = 2A_{1} \\oplus E", {{"\u0393 = 2A", 0}, {"1", -1}, {" \u2295 E", 0}});
		check("\\Gamma^{\\otimes 6} = 715A_{1}", {{"\u0393", 0}, {"\u2297 6", 1}, {" = 715A", 0}, {"1", -1}});
	}

	TEST(GroupTheoryFormattingTests, ProjectedVectorsInUserOrder)
	{
		PointGroupReduction reduction;
		reduction.pointGroupLabel = "C2v";
		reduction.siteLabels = {"a", "b"};
		reduction.projectedVectors = {
			{"A1", 0, 0, {{"sqrt(2)/2", 0.707, 0.0, "\\frac{\\sqrt{2}}{2}"}, {"sqrt(2)/2", 0.707, 0.0, "\\frac{\\sqrt{2}}{2}"}}},
			{"A2", 0, 0, {{"sqrt(2)/2", 0.707, 0.0, "\\frac{\\sqrt{2}}{2}"}, {"-sqrt(2)/2", -0.707, 0.0, "- \\frac{\\sqrt{2}}{2}"}}}};
		const std::vector<std::size_t> order = {1, 0};
		const std::vector<std::string> labels = {"", "b1"};
		EXPECT_EQ(
			FormatProjectedVectors(reduction, order, labels, TableFormat::Markdown),
			"| Irrep | Copy | Row | a | b | Label |\n"
			"|---|---|---|---|---|---|\n"
			"| A\u2082 | 1 | 1 | \u221A2/2 | -\u221A2/2 | b1 |\n"
			"| A\u2081 | 1 | 1 | \u221A2/2 | \u221A2/2 |  |\n");
		EXPECT_EQ(
			FormatProjectedVectors(reduction, order, labels, TableFormat::Latex),
			"\\begin{tabular}{lll|ll|l}\n"
			"Irrep & Copy & Row & a & b & Label \\\\\n"
			"\\hline\n"
			"$A_{2}$ & 1 & 1 & $\\frac{\\sqrt{2}}{2}$ & $- \\frac{\\sqrt{2}}{2}$ & b1 \\\\\n"
			"$A_{1}$ & 1 & 1 & $\\frac{\\sqrt{2}}{2}$ & $\\frac{\\sqrt{2}}{2}$ &  \\\\\n"
			"\\end{tabular}\n");
	}

	TEST(GroupTheoryFormattingTests, ErrorCategories)
	{
		EXPECT_EQ(DescribeErrorCategory("python.groupy.not_installed"), "Python runtime");
		EXPECT_EQ(DescribeErrorCategory("python.groupy.analysis.basis_not_closed"), "Analysis");
		EXPECT_EQ(DescribeErrorCategory("symmetry.basis.empty_selection"), "Selection");
		EXPECT_EQ(DescribeErrorCategory("python.runtime.missing"), "Python runtime");
		EXPECT_EQ(DescribeErrorCategory(""), "Error");
	}
} // namespace
