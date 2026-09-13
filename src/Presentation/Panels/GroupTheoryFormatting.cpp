#include "Core/dspch.hpp"

#include "Presentation/Panels/GroupTheoryFormatting.hpp"

#include <cctype>
#include <string>

namespace DefectStudio
{
	namespace
	{
		[[nodiscard]] std::string FormatUnicodeSubscript(std::string_view digits)
		{
			static constexpr const char *subscripts[] = {
				"₀", "₁", "₂", "₃", "₄", "₅", "₆", "₇", "₈", "₉"};
			std::string result;
			for (const char character : digits)
			{
				if (character >= '0' && character <= '9')
					result += subscripts[character - '0'];
				else
					result += character;
			}
			return result;
		}

		[[nodiscard]] std::string FormatLabel(std::string_view label, LabelStyle style, bool classLabel)
		{
			std::size_t firstCharacter = 0;
			std::string multiplicity;
			while (
				firstCharacter < label.size() &&
				std::isdigit(static_cast<unsigned char>(label[firstCharacter])))
			{
				multiplicity += label[firstCharacter++];
			}

			std::string rest(label.substr(firstCharacter));
			if (classLabel && rest.starts_with("sv"))
				rest = "σv";
			else if (classLabel && rest.starts_with("sh"))
				rest = "σh";
			else if (classLabel && rest.starts_with('s'))
				rest = "σ" + rest.substr(1);

			if (classLabel && rest.starts_with("σ"))
			{
				if (style == LabelStyle::Unicode)
					return multiplicity + rest;

				const std::string sigmaSuffix = rest.substr(2);
				return multiplicity + "\\sigma" +
					(sigmaSuffix.empty() ? "" : "_{" + sigmaSuffix + "}");
			}

			if (rest.empty())
				return multiplicity;

			std::size_t subscriptEnd = 1;
			if (style == LabelStyle::Latex)
			{
				while (
					subscriptEnd < rest.size() &&
					std::isalnum(static_cast<unsigned char>(rest[subscriptEnd])))
				{
					++subscriptEnd;
				}
			}
			else
			{
				while (
					subscriptEnd < rest.size() &&
					std::isdigit(static_cast<unsigned char>(rest[subscriptEnd])))
				{
					++subscriptEnd;
				}
			}

			const std::string head = rest.substr(0, 1);
			const std::string subscript = rest.substr(1, subscriptEnd - 1);
		const std::string suffix = rest.substr(subscriptEnd);
		if (style == LabelStyle::Unicode)
			return multiplicity + head + FormatUnicodeSubscript(subscript) + suffix;
		if (subscript.empty())
			return multiplicity + head + suffix;
		return multiplicity + head + "_{" + subscript + "}" + suffix;
		}
	}

	std::string FormatIrrepLabel(std::string_view label, LabelStyle style)
	{
		return FormatLabel(label, style, false);
	}

	std::string FormatClassLabel(std::string_view label, LabelStyle style)
	{
		return FormatLabel(label, style, true);
	}

	std::string FormatTermLabel(int spinMultiplicity, std::string_view irrepLabel, LabelStyle style)
	{
		if (style == LabelStyle::Unicode)
		{
			static constexpr const char *superscripts[] = {
				"⁰", "¹", "²", "³", "⁴", "⁵", "⁶", "⁷", "⁸", "⁹"};
			const std::string digits = std::to_string(spinMultiplicity);
			std::string result;
			for (const char digit : digits)
				result += superscripts[digit - '0'];
			return result + FormatIrrepLabel(irrepLabel, style);
		}
		return "^{" + std::to_string(spinMultiplicity) + "}" +
			FormatIrrepLabel(irrepLabel, style);
	}

	std::string FormatDecomposition(const std::vector<IrrepMultiplicity> &decomposition, LabelStyle style)
	{
		std::string result = style == LabelStyle::Unicode ? "Γ = " : "\\Gamma = ";
		if (decomposition.empty())
			return result + "0";

		bool first = true;
		for (const IrrepMultiplicity &term : decomposition)
		{
			if (!first)
				result += style == LabelStyle::Unicode ? " ⊕ " : " \\oplus ";
			first = false;
			if (term.multiplicity != 1)
				result += std::to_string(term.multiplicity);
			result += FormatIrrepLabel(term.irrepLabel, style);
		}
		return result;
	}

	std::string FormatCharacterTable(
		const CharacterTable &table,
		const std::vector<ExactCoefficient> &reducible,
		TableFormat format)
	{
		std::string output;
		if (format == TableFormat::Markdown)
		{
			output += "| " + FormatIrrepLabel(table.pointGroupLabel, LabelStyle::Unicode);
			for (const std::string &label : table.classLabels)
				output += " | " + FormatClassLabel(label, LabelStyle::Unicode);
			output += " |\n|---|";
			for (std::size_t index = 0; index < table.classLabels.size(); ++index)
				output += "---|";
			output += "\n";
			for (std::size_t row = 0; row < table.irrepLabels.size(); ++row)
			{
				output += "| " + FormatIrrepLabel(table.irrepLabels[row], LabelStyle::Unicode);
				for (const ExactCoefficient &value : table.characters[row])
					output += " | " + value.exact;
				output += " |\n";
			}
			if (!reducible.empty())
			{
				output += "| Γ";
				for (const ExactCoefficient &value : reducible)
					output += " | " + value.exact;
				output += " |\n";
			}
			return output;
		}

		output += "\\begin{tabular}{l|" + std::string(table.classLabels.size(), 'c') + "}\n$";
		output += FormatIrrepLabel(table.pointGroupLabel, LabelStyle::Latex) + "$";
		for (const std::string &label : table.classLabels)
			output += " & $" + FormatClassLabel(label, LabelStyle::Latex) + "$";
		output += " \\\\\n\\hline\n";
		for (std::size_t row = 0; row < table.irrepLabels.size(); ++row)
		{
			output += "$" + FormatIrrepLabel(table.irrepLabels[row], LabelStyle::Latex) + "$";
			for (const ExactCoefficient &value : table.characters[row])
				output += " & " + value.exact;
			output += " \\\\\n";
		}
		if (!reducible.empty())
		{
			output += "$\\Gamma$";
			for (const ExactCoefficient &value : reducible)
				output += " & " + value.exact;
			output += " \\\\\n";
		}
		output += "\\end{tabular}\n";
		return output;
	}

	std::string FormatMultiplets(const std::vector<MultipletTerm> &multiplets, TableFormat format)
	{
		std::string output;
		if (format == TableFormat::Markdown)
		{
			output = "| Term | Count per row | States |\n|---|---|---|\n";
		}
		else
		{
			output = "\\begin{tabular}{lcc}\nTerm & Count per row & States \\\\\n\\hline\n";
		}

		for (const MultipletTerm &term : multiplets)
		{
			const LabelStyle style =
				format == TableFormat::Markdown ? LabelStyle::Unicode : LabelStyle::Latex;
			const std::string label =
				FormatTermLabel(term.spinMultiplicity, term.irrepLabel, style);
			if (format == TableFormat::Markdown)
			{
				output += "| " + label + " | " + std::to_string(term.countPerRow) +
					" | " + std::to_string(term.totalStates) + " |\n";
			}
			else
			{
				output += "$" + label + "$ & " + std::to_string(term.countPerRow) +
					" & " + std::to_string(term.totalStates) + " \\\\\n";
			}
		}
		if (format == TableFormat::Latex)
			output += "\\end{tabular}\n";
		return output;
	}

	std::string DescribeErrorCategory(std::string_view code)
	{
		if (code.starts_with("symmetry.basis."))
			return "Selection";
		if (code.starts_with("python.groupy.analysis."))
			return "Analysis";
		if (code.starts_with("python."))
			return "Python runtime";
		return "Error";
	}
} // namespace DefectStudio
