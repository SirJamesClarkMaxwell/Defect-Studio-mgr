#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "Domain/Symmetry/PointGroupAnalysis.hpp"

namespace DefectStudio
{
	// Pure text formatting for the group-theory panel: on-screen Unicode labels and the
	// "Copy as Markdown / LaTeX" clipboard text. No ImGui, so it is unit-tested directly.
	enum class LabelStyle
	{
		Unicode, // "A₁", "2C₃", "3σv", "³A₂", "Γ = 2A₁ ⊕ E"
		Latex    // "A_{1}", "2C_{3}", "3\sigma_{v}", "^{3}A_{2}", "\Gamma = 2A_{1} \oplus E" (no $ delimiters)
	};

	enum class TableFormat
	{
		Markdown, // pipe table, Unicode labels
		Latex     // tabular, labels in $...$
	};

	// Irrep or point-group label: first character kept, the alphanumeric run after it is the
	// subscript, trailing primes stay after it. Unicode subscripts digits only ("T2g" -> "T₂g",
	// "C3v" -> "C₃v"); LaTeX subscripts the whole run ("T_{2g}", "C_{3v}"). "E" and "A'" unchanged.
	[[nodiscard]] std::string FormatIrrepLabel(std::string_view label, LabelStyle style);

	// groupy class name: leading count kept, "s" operator -> sigma, then the same subscript rule.
	// "2C3" -> "2C₃" / "2C_{3}", "3sv" -> "3σv" / "3\sigma_{v}", "E" and "i" unchanged.
	[[nodiscard]] std::string FormatClassLabel(std::string_view label, LabelStyle style);

	// (2S+1)Γ term symbol: "³A₂" / "^{3}A_{2}".
	[[nodiscard]] std::string FormatTermLabel(int spinMultiplicity, std::string_view irrepLabel, LabelStyle style);

	// "Γ = 2A₁ ⊕ E"; multiplicity 1 omitted; empty decomposition -> "Γ = 0".
	[[nodiscard]] std::string FormatDecomposition(const std::vector<IrrepMultiplicity> &decomposition, LabelStyle style);

	// Γ^⊗n line: "Γ⊗⁶ = 715A₁ ⊕ 651A₂ ⊕ 1365E" / "\Gamma^{\otimes 6} = 715A_{1} \oplus 651A_{2} \oplus 1365E".
	// Empty decomposition (dimension overflowed) -> "Γ⊗⁶: dimension too large" / "\Gamma^{\otimes 6}: dimension too large".
	[[nodiscard]] std::string FormatTensorPower(
		int power, const std::vector<IrrepMultiplicity> &decomposition, LabelStyle style);

	// Decomposition expanded into the "Active irreps" input text: {A1 x2, E x1} -> "A1, A1, E".
	[[nodiscard]] std::string ActiveIrrepsFromDecomposition(const std::vector<IrrepMultiplicity> &decomposition);

	// One coefficient for display.
	// Unicode: exact prettified - "sqrt(3)/3" -> "√3/3", "I" / "*I" -> "i", remaining "*" -> "·".
	// Latex: ExactCoefficient::latex, or exact when latex is empty (no $ delimiters).
	[[nodiscard]] std::string FormatExactValue(const ExactCoefficient &value, LabelStyle style);

	// A LaTeX-style label split into runs drawn at one baseline level, for the on-screen renderer
	// (ImGui has no sub/superscript). level: -1 subscript, 0 baseline, +1 superscript.
	struct MathSegment
	{
		std::string text; // UTF-8, commands already replaced (\Gamma -> Γ, \sigma -> σ, \oplus -> ⊕, \otimes -> ⊗,
		                  // \rangle -> ⟩, \langle -> ⟨)
		int level = 0;
		bool overline = false; // inside \bar{...}: spin-down orbital
		// Non-empty = a stacked fraction: `text` is the numerator, this the denominator (both plain text).
		std::string denominator;
		// 0 outside \bar; each \bar{...} gets the next id (1, 2, ...) so the renderer draws one bar per
		// orbital. Segments with different ids are never merged.
		int overlineGroup = 0;
	};

	// Parses the Latex output of the formatters in this header: `_{...}`, `^{...}`, `\bar{...}` (may
	// contain `_{...}`), `\frac{num}{den}` (one fraction segment, never merged), `\sqrt{x}` -> "√x" and
	// the commands above. Spaces inside `_{}` / `^{}` are dropped. Adjacent non-fraction runs with the
	// same level and overline are merged. "3\sigma_{v}" -> {"3σ",0},{"v",-1};
	// "-\frac{\sqrt{3}}{2}|a|" -> {"-",0},{"√3",0,false,"2"},{"|a|",0}.
	[[nodiscard]] std::vector<MathSegment> SplitMathSegments(std::string_view latex);

	// "A₁ ⊕ A₂ ⊕ E" / "A_{1} \oplus A_{2} \oplus E"; multiplicity 1 omitted; empty -> "0".
	[[nodiscard]] std::string FormatIrrepSum(const std::vector<IrrepMultiplicity> &sum, LabelStyle style);

	// Γ_i ⊗ Γ_j table (products from ComputeDirectProducts). Corner cell "<group> ⊗ <group>".
	// Markdown: Unicode labels. LaTeX: every label and cell in $...$. Output ends with a newline.
	[[nodiscard]] std::string FormatDirectProductTable(
		const CharacterTable &table, const std::vector<std::vector<std::vector<IrrepMultiplicity>>> &products,
		TableFormat format);

	// Active space picked with the "Active" checkboxes of the projected-vectors table.
	struct ActiveSpaceSelection
	{
		std::vector<std::string> irreps;        // PointGroupAnalysisRequest::activeOrbitalIrreps
		std::vector<std::string> orbitalLabels; // PointGroupAnalysisRequest::activeOrbitalLabels
	};

	// `activeVectors` and `physicalLabels` are indexed by ORIGINAL vector index (missing = false / "").
	// A copy (irrep, occurrenceIndex) is active when any of its rows is flagged. Shells follow the
	// display order (`order`) of each active copy's first displayed row. Labels: one per row of each
	// shell in irrepRow order, FormatIrrepLabel(label, Latex) or "" when blank; `orbitalLabels` is empty
	// when every one of them is blank.
	[[nodiscard]] ActiveSpaceSelection BuildActiveSpaceSelection(
		const PointGroupReduction &reduction, const std::vector<std::size_t> &order,
		const std::vector<bool> &activeVectors, const std::vector<std::string> &physicalLabels);

	// Occupied shells as LaTeX markup: {2,2} over {a_{1}, e} -> "a_{1}^{2}e^{2}"; count 1 has no
	// exponent, empty shells are omitted.
	[[nodiscard]] std::string FormatConfiguration(
		const std::vector<ActiveShell> &shells, const std::vector<int> &configuration);

	// "|<term>[(<copy+1>)]; [i=<row+1>, ]m_{s}=<m_s>\rangle = <expansion>", term from FormatTermLabel
	// Latex, copy suffix only for copyIndex > 0, row only for degenerate irreps, m_s as an integer or
	// "k/2". Determinant "|a_{1}\bar{a_{1}}e_{x}e_{y}|" (bar = spin down). Coefficients ±1 are omitted;
	// with two or more terms of equal |c| ≠ 1 the magnitude is factored out: "<|c|> (|d1| - |d2|)".
	// Terms are joined with " + " / " - "; a negative first term starts with "-".
	// Latex: coefficients from ExactCoefficient::latex. Unicode: coefficients from FormatExactValue
	// Unicode, everything else stays the LaTeX markup SplitMathSegments draws.
	[[nodiscard]] std::string FormatWavefunction(
		const MultipletWavefunction &state, const std::vector<std::string> &orbitalLabels, LabelStyle style);

	// Every state of one term as a LaTeX aligned block:
	// "\begin{aligned}\n" + per state "<config> & <FormatWavefunction Latex> \\\n" + "\end{aligned}\n",
	// <config> written only when it differs from the previous state's (else empty). "" when the term
	// has no states.
	[[nodiscard]] std::string FormatTermWavefunctions(
		const PointGroupAnalysisResult &result, std::string_view irrepLabel, int spinMultiplicity);

	// Projected vectors in a user-chosen row order. `order` holds indices into
	// reduction.projectedVectors; `physicalLabels` is indexed by the ORIGINAL vector index (missing
	// entries = empty). Columns: Irrep, Copy (1-based), Row (1-based), one per site, Label.
	// Markdown uses FormatExactValue Unicode; LaTeX puts irreps and non-empty-latex values in $...$.
	[[nodiscard]] std::string FormatProjectedVectors(
		const PointGroupReduction &reduction, const std::vector<std::size_t> &order,
		const std::vector<std::string> &physicalLabels, TableFormat format);

	// Character table with a final Γ row of reducible characters when `reducibleCharacters` is
	// non-empty. Markdown uses ExactCoefficient::exact verbatim; LaTeX wraps a value in $...$ only when
	// its latex is non-empty. Output ends with a newline.
	[[nodiscard]] std::string FormatCharacterTable(
		const CharacterTable &table, const std::vector<ExactCoefficient> &reducibleCharacters, TableFormat format);

	// Columns: Term, Count per row, States. Output ends with a newline.
	[[nodiscard]] std::string FormatMultiplets(const std::vector<MultipletTerm> &multiplets, TableFormat format);

	// Panel error heading from a StructuredError code:
	// "python.groupy.not_installed" and other "python." codes -> "Python runtime",
	// "python.groupy.analysis.*" -> "Analysis", "symmetry.basis.*" -> "Selection", anything else -> "Error".
	[[nodiscard]] std::string DescribeErrorCategory(std::string_view code);
} // namespace DefectStudio
