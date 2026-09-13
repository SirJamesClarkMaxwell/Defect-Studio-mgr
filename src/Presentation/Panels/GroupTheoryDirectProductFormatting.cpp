#include "Core/dspch.hpp"

#include "Presentation/Panels/GroupTheoryFormatting.hpp"

namespace DefectStudio
{
	std::string FormatIrrepSum(const std::vector<IrrepMultiplicity> &sum, LabelStyle style)
	{
		if (sum.empty())
			return "0";
		std::string result;
		for (const IrrepMultiplicity &entry : sum)
		{
			if (!result.empty())
				result += style == LabelStyle::Unicode ? " ⊕ " : " \\oplus ";
			if (entry.multiplicity != 1)
				result += std::to_string(entry.multiplicity);
			result += FormatIrrepLabel(entry.irrepLabel, style);
		}
		return result;
	}

	std::string FormatDirectProductTable(
		const CharacterTable &table,
		const std::vector<std::vector<std::vector<IrrepMultiplicity>>> &products,
		TableFormat format)
	{
		const bool markdown = format == TableFormat::Markdown;
		std::string output = markdown ? "| " : "\\begin{tabular}{l|" + std::string(table.irrepLabels.size(), 'c') + "}\n$";
		output += FormatIrrepLabel(table.pointGroupLabel, markdown ? LabelStyle::Unicode : LabelStyle::Latex) +
			(markdown ? " ⊗ " : " \\otimes ") +
			FormatIrrepLabel(table.pointGroupLabel, markdown ? LabelStyle::Unicode : LabelStyle::Latex);
		if (markdown)
		{
			for (const std::string &label : table.irrepLabels)
				output += " | " + FormatIrrepLabel(label, LabelStyle::Unicode);
			output += " |\n|---|";
		}
		else
		{
			output += "$";
			for (const std::string &label : table.irrepLabels)
				output += " & $" + FormatIrrepLabel(label, LabelStyle::Latex) + "$";
			output += " \\\\\n\\hline\n";
		}
		if (markdown)
			for (std::size_t i = 0; i < table.irrepLabels.size(); ++i)
				output += "---|";
		for (std::size_t row = 0; row < table.irrepLabels.size(); ++row)
		{
			if (markdown)
				output += "\n| " + FormatIrrepLabel(table.irrepLabels[row], LabelStyle::Unicode) + " |";
			else
				output += "$" + FormatIrrepLabel(table.irrepLabels[row], LabelStyle::Latex) + "$";
			for (std::size_t column = 0; column < table.irrepLabels.size(); ++column)
				output += markdown ? " " + FormatIrrepSum(products[row][column], LabelStyle::Unicode) + " |" :
					" & $" + FormatIrrepSum(products[row][column], LabelStyle::Latex) + "$";
			if (!markdown)
				output += " \\\\\n";
		}
		return markdown ? output + "\n" : output + "\\end{tabular}\n";
	}
} // namespace DefectStudio
