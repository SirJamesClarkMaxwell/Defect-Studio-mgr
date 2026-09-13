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
		std::string text; // UTF-8, commands already replaced (\Gamma -> Γ, \sigma -> σ, \oplus -> ⊕, \otimes -> ⊗)
		int level = 0;
	};

	// Parses the Latex output of the formatters above: `_{...}`, `^{...}` and the four commands.
	// Adjacent runs at the same level are merged. "3\sigma_{v}" -> {"3σ",0},{"v",-1}.
	[[nodiscard]] std::vector<MathSegment> SplitMathSegments(std::string_view latex);

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
