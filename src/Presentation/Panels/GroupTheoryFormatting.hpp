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

	// Character table with a final Γ row of reducible characters when `reducibleCharacters` is
	// non-empty. Characters use ExactCoefficient::exact verbatim. Output ends with a newline.
	[[nodiscard]] std::string FormatCharacterTable(
		const CharacterTable &table, const std::vector<ExactCoefficient> &reducibleCharacters, TableFormat format);

	// Columns: Term, Count per row, States. Output ends with a newline.
	[[nodiscard]] std::string FormatMultiplets(const std::vector<MultipletTerm> &multiplets, TableFormat format);

	// Panel error heading from a StructuredError code:
	// "python.groupy.not_installed" and other "python." codes -> "Python runtime",
	// "python.groupy.analysis.*" -> "Analysis", "symmetry.basis.*" -> "Selection", anything else -> "Error".
	[[nodiscard]] std::string DescribeErrorCategory(std::string_view code);
} // namespace DefectStudio
