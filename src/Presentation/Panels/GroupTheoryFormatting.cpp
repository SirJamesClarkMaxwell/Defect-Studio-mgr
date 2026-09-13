#include "Core/dspch.hpp"

#include "Presentation/Panels/GroupTheoryFormatting.hpp"

#include <cctype>
#include <string>
#include <utility>

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

		[[nodiscard]] std::string FormatUnicodeSuperscript(std::string_view digits)
		{
			static constexpr const char *superscripts[] = {
				"⁰", "¹", "²", "³", "⁴", "⁵", "⁶", "⁷", "⁸", "⁹"};
			std::string result;
			for (const char character : digits)
			{
				if (character >= '0' && character <= '9')
					result += superscripts[character - '0'];
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

		[[nodiscard]] std::string FormatLatexCell(const ExactCoefficient &value)
		{
			return value.latex.empty() ? value.exact : "$" + value.latex + "$";
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

	std::string FormatTensorPower(
		int power, const std::vector<IrrepMultiplicity> &decomposition, LabelStyle style)
	{
		const std::string prefix = style == LabelStyle::Unicode ? "Γ⊗" : "\\Gamma^{\\otimes ";
		const std::string exponent = style == LabelStyle::Unicode ? FormatUnicodeSuperscript(std::to_string(power)) : std::to_string(power) + "}";
		std::string result = prefix + exponent;
		if (decomposition.empty())
			return result + (style == LabelStyle::Unicode ? ": dimension too large" : ": dimension too large");
		result += " = ";
		for (std::size_t index = 0; index < decomposition.size(); ++index)
		{
			if (index != 0)
				result += style == LabelStyle::Unicode ? " ⊕ " : " \\oplus ";
			if (decomposition[index].multiplicity != 1)
				result += std::to_string(decomposition[index].multiplicity);
			result += FormatIrrepLabel(decomposition[index].irrepLabel, style);
		}
		return result;
	}

	std::string ActiveIrrepsFromDecomposition(const std::vector<IrrepMultiplicity> &decomposition)
	{
		std::string result;
		for (const IrrepMultiplicity &entry : decomposition)
			for (int index = 0; index < entry.multiplicity; ++index)
			{
				if (!result.empty())
					result += ", ";
				result += entry.irrepLabel;
			}
		return result;
	}

	std::string FormatExactValue(const ExactCoefficient &value, LabelStyle style)
	{
		if (style == LabelStyle::Latex)
			return value.latex.empty() ? value.exact : value.latex;
		std::string result = value.exact;
		std::string::size_type position = 0;
		while ((position = result.find("sqrt(", position)) != std::string::npos)
		{
			const auto close = result.find(')', position + 5);
			if (close == std::string::npos)
				break;
			result.replace(position, close - position + 1, "√" + result.substr(position + 5, close - position - 5));
			position += 1;
		}
		while ((position = result.find("*I")) != std::string::npos)
			result.replace(position, 2, "i");
		while ((position = result.find("I")) != std::string::npos)
			result.replace(position, 1, "i");
		while ((position = result.find('*')) != std::string::npos)
			result.replace(position, 1, "·");
		return result;
	}

	std::vector<MathSegment> SplitMathSegments(std::string_view latex)
	{
		std::vector<MathSegment> result;
		const auto append = [&result](std::string text, int level) {
			if (text.empty()) return;
			if (!result.empty() && result.back().level == level) result.back().text += text;
			else result.push_back({std::move(text), level});
		};
		for (std::size_t index = 0; index < latex.size();)
		{
			if ((latex[index] == '_' || latex[index] == '^') && index + 1 < latex.size())
			{
				const int level = latex[index++] == '^' ? 1 : -1;
				std::string text;
				if (latex[index] == '{')
				{
					++index;
					while (index < latex.size() && latex[index] != '}') text += latex[index++];
					if (index < latex.size()) ++index;
				}
				else text += latex[index++];
				for (const auto &segment : SplitMathSegments(text)) append(segment.text, level + segment.level);
				continue;
			}
			if (latex[index] == '\\')
			{
				const std::size_t begin = ++index;
				while (index < latex.size() && std::isalpha(static_cast<unsigned char>(latex[index]))) ++index;
				const std::string command(latex.substr(begin, index - begin));
				if (command == "Gamma") append("Γ", 0);
				else if (command == "sigma") append("σ", 0);
				else if (command == "oplus") append("⊕", 0);
				else if (command == "otimes") append("⊗", 0);
				else append("\\" + command, 0);
				continue;
			}
			append(std::string(1, latex[index++]), 0);
		}
		return result;
	}

	std::string FormatProjectedVectors(
		const PointGroupReduction &reduction, const std::vector<std::size_t> &order,
		const std::vector<std::string> &physicalLabels, TableFormat format)
	{
		const bool markdown = format == TableFormat::Markdown;
		std::string output = markdown ? "| Irrep | Copy | Row |" : "\\begin{tabular}{lll|";
		if (!markdown)
			output += std::string(reduction.siteLabels.size(), 'l') + "|l}\nIrrep & Copy & Row & ";
		for (std::size_t index = 0; index < reduction.siteLabels.size(); ++index)
		{
			if (markdown) output += " " + reduction.siteLabels[index] + " |";
			else output += std::string(index ? " & " : "") + reduction.siteLabels[index];
		}
		if (markdown) output += " Label |\n|---|---|---|";
		else output += " & Label \\\\\n\\hline\n";
		if (markdown)
			for (std::size_t index = 0; index < reduction.siteLabels.size() + 1; ++index) output += "---|";
		for (const std::size_t vectorIndex : order)
		{
			if (vectorIndex >= reduction.projectedVectors.size()) continue;
			const auto &vector = reduction.projectedVectors[vectorIndex];
			const std::string label = vectorIndex < physicalLabels.size() ? physicalLabels[vectorIndex] : "";
			if (markdown)
			{
				output += "\n| " + FormatIrrepLabel(vector.irrepLabel, LabelStyle::Unicode) + " | " + std::to_string(vector.occurrenceIndex + 1) + " | " + std::to_string(vector.irrepRow + 1) + " |";
				for (const auto &value : vector.coefficients) output += " " + FormatExactValue(value, LabelStyle::Unicode) + " |";
				output += " " + label + " |";
			}
			else
			{
				output += "$" + FormatIrrepLabel(vector.irrepLabel, LabelStyle::Latex) + "$ & " + std::to_string(vector.occurrenceIndex + 1) + " & " + std::to_string(vector.irrepRow + 1);
				for (const auto &value : vector.coefficients) output += " & " + FormatLatexCell(value);
				output += " & " + label + " \\\\\n";
			}
		}
		if (!markdown) output += "\\end{tabular}\n";
		else output += "\n";
		return output;
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
				output += " & " + FormatLatexCell(value);
			output += " \\\\\n";
		}
		if (!reducible.empty())
		{
			output += "$\\Gamma$";
			for (const ExactCoefficient &value : reducible)
				output += " & " + FormatLatexCell(value);
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
