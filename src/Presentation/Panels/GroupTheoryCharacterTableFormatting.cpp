#include "Core/dspch.hpp"

#include "Presentation/Panels/GroupTheoryFormatting.hpp"

#include <string>

namespace DefectStudio
{
	namespace
	{
		[[nodiscard]] std::string FormatLatexCell(const ExactCoefficient &value)
		{
			if (value.latex.empty())
				return value.exact;
			return "$" + value.latex + "$";
		}
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
			for (std::size_t index = 0; index < table.classLabels.size(); ++index)
				output += " | " + FormatClassLabel(table.classLabels[index], LabelStyle::Unicode);
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
		for (std::size_t index = 0; index < table.classLabels.size(); ++index)
			output += " & $" + FormatClassLabel(table.classLabels[index], LabelStyle::Latex) + "$";
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
} // namespace DefectStudio
