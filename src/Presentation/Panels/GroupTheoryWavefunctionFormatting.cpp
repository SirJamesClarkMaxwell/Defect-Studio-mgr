#include "Core/dspch.hpp"

#include "Presentation/Panels/GroupTheoryFormatting.hpp"

#include <algorithm>
#include <cmath>

namespace DefectStudio
{
	std::string FormatConfiguration(const std::vector<ActiveShell> &shells, const std::vector<int> &configuration)
	{
		std::string result;
		for (std::size_t index = 0; index < shells.size() && index < configuration.size(); ++index)
			if (configuration[index] > 0)
			{
				result += shells[index].label;
				if (configuration[index] != 1)
					result += "^{" + std::to_string(configuration[index]) + "}";
			}
		return result;
	}

	std::string FormatWavefunction(const MultipletWavefunction &state, const std::vector<std::string> &orbitalLabels, LabelStyle style)
	{
		std::string prefix = "|" + FormatTermLabel(state.spinMultiplicity, state.irrepLabel, LabelStyle::Latex);
		if (state.copyIndex > 0)
			prefix += "(" + std::to_string(state.copyIndex + 1) + ")";
		prefix += "; ";
		// ponytail: Mulliken prefixes are the stable metadata available here; adding dimensions to the domain state would broaden the contract.
		const bool degenerate = !state.irrepLabel.empty() &&
			(state.irrepLabel.front() == 'E' || state.irrepLabel.front() == 'T' ||
			 state.irrepLabel.front() == 'G' || state.irrepLabel.front() == 'H');
		if (degenerate)
			prefix += "i=" + std::to_string(state.irrepRow + 1) + ", ";
		prefix += "m_{s}=" +
			(state.twiceMs % 2 == 0 ? std::to_string(state.twiceMs / 2) : std::to_string(state.twiceMs) + "/2") +
			"\\rangle = ";
		std::vector<std::string> determinants;
		for (const DeterminantTerm &determinant : state.determinants)
		{
			std::string ket = "|";
			for (const SpinOrbital &orbital : determinant.occupied)
			{
				const std::string label = orbital.orbitalIndex < static_cast<int>(orbitalLabels.size()) ? orbitalLabels[orbital.orbitalIndex] : "?";
				ket += orbital.spinUp ? label : "\\bar{" + label + "}";
			}
			determinants.push_back(ket + "|");
		}
		if (determinants.empty())
			return prefix + "0";
		const bool factor = state.determinants.size() >= 2 &&
			std::abs(state.determinants[0].coefficient.numeric) > 1e-12 &&
			std::all_of(state.determinants.begin(), state.determinants.end(), [&](const DeterminantTerm &term) {
				return std::abs(std::abs(term.coefficient.numeric) - std::abs(state.determinants[0].coefficient.numeric)) < 1e-9 &&
					std::abs(std::abs(term.coefficient.numeric) - 1.0) > 1e-9;
			});
		std::string expansion;
		if (factor)
			expansion = FormatExactValue(state.determinants[0].coefficient, style) + " (";
		for (std::size_t index = 0; index < determinants.size(); ++index)
		{
			const DeterminantTerm &term = state.determinants[index];
			if (index > 0)
				expansion += term.coefficient.numeric < 0 ? " - " : " + ";
			else if (term.coefficient.numeric < 0)
				expansion += "-";
			if (!factor && std::abs(std::abs(term.coefficient.numeric) - 1.0) > 1e-9)
			{
				std::string coefficient = FormatExactValue(term.coefficient, style);
				if (coefficient.starts_with("-"))
					coefficient.erase(0, 1);
				expansion += coefficient;
			}
			expansion += determinants[index];
		}
		if (factor)
			expansion += ")";
		return prefix + expansion;
	}

	std::string FormatTermWavefunctions(const PointGroupAnalysisResult &result, std::string_view irrepLabel, int spinMultiplicity)
	{
		std::string output;
		std::string previous;
		for (const MultipletWavefunction &state : result.wavefunctions)
			if (state.irrepLabel == irrepLabel && state.spinMultiplicity == spinMultiplicity)
			{
				const std::string configuration = FormatConfiguration(result.activeShells, state.configuration);
				if (configuration != previous)
					output += configuration;
				output += " & " + FormatWavefunction(state, result.activeOrbitalLabels, LabelStyle::Latex) + " \\\\\n";
				previous = configuration;
			}
		return output.empty() ? "" : "\\begin{aligned}\n" + output + "\\end{aligned}\n";
	}
} // namespace DefectStudio
