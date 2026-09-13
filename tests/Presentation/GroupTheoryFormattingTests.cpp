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
				EXPECT_EQ(actual[i].overline, expected[i].overline) << latex << " segment " << i;
				EXPECT_EQ(actual[i].denominator, expected[i].denominator) << latex << " segment " << i;
			}
		};
		check("-\\frac{\\sqrt{3}}{2}|a|", {{"-", 0}, {"√3", 0, false, "2"}, {"|a|", 0}});
		check("\\frac{1}{2}\\frac{1}{2}", {{"1", 0, false, "2"}, {"1", 0, false, "2"}});
		check("\\sqrt{2}", {{"√2", 0}});
		check("|\\bar{a_{1}}e_{x}\\rangle", {{"|", 0}, {"a", 0, true}, {"1", -1, true}, {"e", 0}, {"x", -1}, {"⟩", 0}});
		check("E", {{"E", 0}});
		check("3\\sigma_{v}", {{"3\u03C3", 0}, {"v", -1}});
		check("^{3}A_{2}", {{"3", 1}, {"A", 0}, {"2", -1}});
		check("C_{3v}", {{"C", 0}, {"3v", -1}});
		check("A_{1}'", {{"A", 0}, {"1", -1}, {"'", 0}});
		check("\\Gamma = 2A_{1} \\oplus E", {{"\u0393 = 2A", 0}, {"1", -1}, {" \u2295 E", 0}});
		check("\\Gamma^{\\otimes 6} = 715A_{1}", {{"\u0393", 0}, {"\u2297" "6", 1}, {" = 715A", 0}, {"1", -1}});
	}

	TEST(GroupTheoryFormattingTests, IrrepSumAndDirectProductTable)
	{
		EXPECT_EQ(FormatIrrepSum({{"A1", 1, 1}, {"A2", 1, 1}, {"E", 1, 2}}, LabelStyle::Latex), "A_{1} \\oplus A_{2} \\oplus E");
		EXPECT_EQ(FormatIrrepSum({{"E", 2, 2}}, LabelStyle::Unicode), "2E");
		EXPECT_EQ(FormatIrrepSum({}, LabelStyle::Unicode), "0");

		const std::vector<IrrepMultiplicity> a1{{"A1", 1, 1}}, a2{{"A2", 1, 1}}, e{{"E", 1, 2}};
		const std::vector<IrrepMultiplicity> all{{"A1", 1, 1}, {"A2", 1, 1}, {"E", 1, 2}};
		const std::vector<std::vector<std::vector<IrrepMultiplicity>>> products{{a1, a2, e}, {a2, a1, e}, {e, e, all}};
		EXPECT_EQ(
			FormatDirectProductTable(MakeC3vTable(), products, TableFormat::Markdown),
			"| C\u2083v \u2297 C\u2083v | A\u2081 | A\u2082 | E |\n"
			"|---|---|---|---|\n"
			"| A\u2081 | A\u2081 | A\u2082 | E |\n"
			"| A\u2082 | A\u2082 | A\u2081 | E |\n"
			"| E | E | E | A\u2081 \u2295 A\u2082 \u2295 E |\n");
		EXPECT_EQ(
			FormatDirectProductTable(MakeC3vTable(), products, TableFormat::Latex),
			"\\begin{tabular}{l|ccc}\n"
			"$C_{3v} \\otimes C_{3v}$ & $A_{1}$ & $A_{2}$ & $E$ \\\\\n"
			"\\hline\n"
			"$A_{1}$ & $A_{1}$ & $A_{2}$ & $E$ \\\\\n"
			"$A_{2}$ & $A_{2}$ & $A_{1}$ & $E$ \\\\\n"
			"$E$ & $E$ & $E$ & $A_{1} \\oplus A_{2} \\oplus E$ \\\\\n"
			"\\end{tabular}\n");
	}

	PointGroupReduction MakeNvReduction()
	{
		PointGroupReduction reduction;
		reduction.pointGroupLabel = "C3v";
		reduction.siteLabels = {"C1", "N", "C2", "C3"};
		reduction.projectedVectors = {{"A1", 0, 0, {}}, {"A1", 1, 0, {}}, {"E", 0, 0, {}}, {"E", 0, 1, {}}};
		return reduction;
	}

	TEST(GroupTheoryFormattingTests, ActiveSpaceFromCheckboxes)
	{
		const PointGroupReduction reduction = MakeNvReduction();
		// Displayed: A1 copy 2, A1 copy 1, E row 2, E row 1. Only E row 2 is flagged - the whole E copy is active.
		const std::vector<std::size_t> order{1, 0, 3, 2};
		ActiveSpaceSelection selection =
			BuildActiveSpaceSelection(reduction, order, {true, false, false, true}, {"a1", "a1'", "ex", "ey"});
		EXPECT_EQ(selection.irreps, (std::vector<std::string>{"A1", "E"}));
		EXPECT_EQ(selection.orbitalLabels, (std::vector<std::string>{"a_{1}", "e_{x}", "e_{y}"}));

		// Shell order follows the display: A1 copy 2 (N) is shown first.
		selection = BuildActiveSpaceSelection(reduction, order, {true, true, false, false}, {"a1", "", "", ""});
		EXPECT_EQ(selection.irreps, (std::vector<std::string>{"A1", "A1"}));
		EXPECT_EQ(selection.orbitalLabels, (std::vector<std::string>{"", "a_{1}"}));

		selection = BuildActiveSpaceSelection(reduction, order, {false, false, true, false}, {});
		EXPECT_EQ(selection.irreps, (std::vector<std::string>{"E"}));
		EXPECT_TRUE(selection.orbitalLabels.empty());

		EXPECT_TRUE(BuildActiveSpaceSelection(reduction, order, {}, {}).irreps.empty());
	}

	const ExactCoefficient kOne{"1", 1.0, 0.0, "1"};
	const ExactCoefficient kHalfSqrt2{"sqrt(2)/2", 0.7071067811865476, 0.0, "\\frac{\\sqrt{2}}{2}"};
	const ExactCoefficient kMinusHalfSqrt2{"-sqrt(2)/2", -0.7071067811865476, 0.0, "- \\frac{\\sqrt{2}}{2}"};
	const std::vector<std::string> kNvLabels{"a_{1}", "e_{x}", "e_{y}"};

	MultipletWavefunction TripletA2(int twiceMs)
	{
		MultipletWavefunction state{"A2", 3, 0, 0, twiceMs, {2, 2}, {}};
		if (twiceMs == 2)
			state.determinants = {{kOne, {{0, true}, {0, false}, {1, true}, {2, true}}}};
		else
			state.determinants = {
				{kHalfSqrt2, {{0, true}, {0, false}, {1, true}, {2, false}}},
				{kHalfSqrt2, {{0, true}, {0, false}, {1, false}, {2, true}}}};
		return state;
	}

	TEST(GroupTheoryFormattingTests, Configurations)
	{
		const std::vector<ActiveShell> shells{{"A1", "a_{1}", 0, 1}, {"E", "e", 1, 2}};
		EXPECT_EQ(FormatConfiguration(shells, {2, 2}), "a_{1}^{2}e^{2}");
		EXPECT_EQ(FormatConfiguration(shells, {1, 3}), "a_{1}e^{3}");
		EXPECT_EQ(FormatConfiguration(shells, {0, 4}), "e^{4}");
	}

	TEST(GroupTheoryFormattingTests, Wavefunctions)
	{
		EXPECT_EQ(
			FormatWavefunction(TripletA2(2), kNvLabels, LabelStyle::Latex),
			"|^{3}A_{2}; m_{s}=1\\rangle = |a_{1}\\bar{a_{1}}e_{x}e_{y}|");
		EXPECT_EQ(
			FormatWavefunction(TripletA2(0), kNvLabels, LabelStyle::Latex),
			"|^{3}A_{2}; m_{s}=0\\rangle = \\frac{\\sqrt{2}}{2} (|a_{1}\\bar{a_{1}}e_{x}\\bar{e_{y}}| + |a_{1}\\bar{a_{1}}\\bar{e_{x}}e_{y}|)");
		EXPECT_EQ(
			FormatWavefunction(TripletA2(0), kNvLabels, LabelStyle::Unicode),
			"|^{3}A_{2}; m_{s}=0\\rangle = \u221A2/2 (|a_{1}\\bar{a_{1}}e_{x}\\bar{e_{y}}| + |a_{1}\\bar{a_{1}}\\bar{e_{x}}e_{y}|)");

		// Degenerate irrep, second copy, relative minus sign.
		const MultipletWavefunction singletE{
			"E", 1, 1, 1, 0, {2, 2},
			{{kHalfSqrt2, {{0, true}, {0, false}, {1, true}, {1, false}}},
			 {kMinusHalfSqrt2, {{0, true}, {0, false}, {2, true}, {2, false}}}}};
		EXPECT_EQ(
			FormatWavefunction(singletE, kNvLabels, LabelStyle::Latex),
			"|^{1}E(2); i=2, m_{s}=0\\rangle = \\frac{\\sqrt{2}}{2} (|a_{1}\\bar{a_{1}}e_{x}\\bar{e_{x}}| - |a_{1}\\bar{a_{1}}e_{y}\\bar{e_{y}}|)");

		// Unequal magnitudes are not factored; half-integer m_s.
		const MultipletWavefunction doublet{
			"A1", 2, 0, 0, -1, {1, 0},
			{{{"sqrt(3)/2", 0.866, 0.0, "\\frac{\\sqrt{3}}{2}"}, {{0, false}}},
			 {{"-1/2", -0.5, 0.0, "- \\frac{1}{2}"}, {{1, false}}}}};
		EXPECT_EQ(
			FormatWavefunction(doublet, kNvLabels, LabelStyle::Unicode),
			"|^{2}A_{1}; m_{s}=-1/2\\rangle = \u221A3/2|\\bar{a_{1}}| - 1/2|\\bar{e_{x}}|");
	}

	TEST(GroupTheoryFormattingTests, TermWavefunctionsBlock)
	{
		PointGroupAnalysisResult result;
		result.activeShells = {{"A1", "a_{1}", 0, 1}, {"E", "e", 1, 2}};
		result.activeOrbitalLabels = kNvLabels;
		result.wavefunctions = {TripletA2(2), TripletA2(0), MultipletWavefunction{"E", 1, 0, 0, 0, {1, 3}, {}}};
		EXPECT_EQ(
			FormatTermWavefunctions(result, "A2", 3),
			"\\begin{aligned}\n"
			"a_{1}^{2}e^{2} & |^{3}A_{2}; m_{s}=1\\rangle = |a_{1}\\bar{a_{1}}e_{x}e_{y}| \\\\\n"
			" & |^{3}A_{2}; m_{s}=0\\rangle = \\frac{\\sqrt{2}}{2} (|a_{1}\\bar{a_{1}}e_{x}\\bar{e_{y}}| + |a_{1}\\bar{a_{1}}\\bar{e_{x}}e_{y}|) \\\\\n"
			"\\end{aligned}\n");
		EXPECT_EQ(FormatTermWavefunctions(result, "T2", 3), "");
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
