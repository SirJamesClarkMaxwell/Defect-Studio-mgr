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

	TEST(GroupTheoryFormattingTests, ErrorCategories)
	{
		EXPECT_EQ(DescribeErrorCategory("python.groupy.not_installed"), "Python runtime");
		EXPECT_EQ(DescribeErrorCategory("python.groupy.analysis.basis_not_closed"), "Analysis");
		EXPECT_EQ(DescribeErrorCategory("symmetry.basis.empty_selection"), "Selection");
		EXPECT_EQ(DescribeErrorCategory("python.runtime.missing"), "Python runtime");
		EXPECT_EQ(DescribeErrorCategory(""), "Error");
	}
} // namespace
