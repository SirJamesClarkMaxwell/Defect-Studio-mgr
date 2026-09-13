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

	std::string FormatProjectedVectors(
		const PointGroupReduction &reduction,
		const std::vector<std::size_t> &order,
		const std::vector<std::string> &physicalLabels,
		TableFormat format)
	{
		const bool markdown = format == TableFormat::Markdown;
		std::string output = markdown ? "| Irrep | Copy | Row |" : "\\begin{tabular}{lll|";
		if (!markdown)
			output += std::string(reduction.siteLabels.size(), 'l') + "|l}\nIrrep & Copy & Row & ";
		for (std::size_t index = 0; index < reduction.siteLabels.size(); ++index)
		{
			if (markdown)
				output += " " + reduction.siteLabels[index] + " |";
			else
				output += std::string(index ? " & " : "") + reduction.siteLabels[index];
		}
		if (markdown)
			output += " Label |\n|---|---|---|";
		else
			output += " & Label \\\\\n\\hline\n";
		if (markdown)
			for (std::size_t index = 0; index < reduction.siteLabels.size() + 1; ++index)
				output += "---|";
		for (const std::size_t vectorIndex : order)
		{
			if (vectorIndex >= reduction.projectedVectors.size())
				continue;
			const auto &vector = reduction.projectedVectors[vectorIndex];
			const std::string label =
				vectorIndex < physicalLabels.size() ? physicalLabels[vectorIndex] : "";
			if (markdown)
			{
				output += "\n| " + FormatIrrepLabel(vector.irrepLabel, LabelStyle::Unicode) +
					" | " + std::to_string(vector.occurrenceIndex + 1) +
					" | " + std::to_string(vector.irrepRow + 1) + " |";
				for (const auto &value : vector.coefficients)
					output += " " + FormatExactValue(value, LabelStyle::Unicode) + " |";
				output += " " + label + " |";
			}
			else
			{
				output += "$" + FormatIrrepLabel(vector.irrepLabel, LabelStyle::Latex) +
					"$ & " + std::to_string(vector.occurrenceIndex + 1) +
					" & " + std::to_string(vector.irrepRow + 1);
				for (const auto &value : vector.coefficients)
					output += " & " + FormatLatexCell(value);
				output += " & " + label + " \\\\\n";
			}
		}
		if (!markdown)
			output += "\\end{tabular}\n";
		else
			output += "\n";
		return output;
	}
} // namespace DefectStudio
