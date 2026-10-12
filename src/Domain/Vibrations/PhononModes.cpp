#include "Core/dspch.hpp"

#include "Domain/Vibrations/PhononModes.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace DefectStudio
{
	namespace
	{
		std::vector<glm::dvec3> PhysicalDisplacements(const PhononAnalysisResult &result, std::size_t mode)
		{
			const auto atoms = result.AtomCount();
			if (mode >= result.ModeCount() || atoms == 0 || result.massesAmu.size() != atoms ||
				mode >= result.eigenvectors.size() / 3 / atoms)
				return {};
			std::vector<glm::dvec3> displacements;
			displacements.reserve(atoms);
			for (std::size_t atom = 0; atom < atoms; ++atom)
			{
				const double mass = result.massesAmu[atom];
				if (!std::isfinite(mass) || mass <= 0.0)
					return {};
				const auto offset = (mode * atoms + atom) * 3;
				const glm::dvec3 displacement = glm::dvec3(result.eigenvectors[offset],
					result.eigenvectors[offset + 1], result.eigenvectors[offset + 2]) / std::sqrt(mass);
				if (!std::isfinite(displacement.x) || !std::isfinite(displacement.y) || !std::isfinite(displacement.z))
					return {};
				displacements.push_back(displacement);
			}
			return displacements;
		}

		double LongestDisplacement(const std::vector<glm::dvec3> &displacements)
		{
			double longest = 0.0;
			for (const auto &displacement : displacements)
				longest = std::max(longest, glm::length(displacement));
			return longest;
		}
	} // namespace

	std::vector<ModeArrow> BuildModeArrows(const PhononAnalysisResult &result, std::size_t mode,
		double participationThreshold, double maxLengthAngstrom)
	{
		const auto atoms = result.AtomCount();
		if (atoms == 0 || mode >= result.ModeCount() || mode >= result.participation.size() / atoms ||
			!std::isfinite(participationThreshold) || !std::isfinite(maxLengthAngstrom) || maxLengthAngstrom < 0.0)
			return {};
		const auto displacements = PhysicalDisplacements(result, mode);
		const double longest = LongestDisplacement(displacements);
		std::vector<ModeArrow> arrows;
		for (std::size_t atom = 0; atom < displacements.size(); ++atom)
		{
			const double participation = result.participation[mode * atoms + atom];
			if (participation >= participationThreshold)
				arrows.push_back({atom, longest > 0.0 ? displacements[atom] * (maxLengthAngstrom / longest) :
					glm::dvec3(0.0), participation});
		}
		return arrows;
	}

	std::size_t AtomsCarryingFraction(const PhononAnalysisResult &result, std::size_t mode, double fraction)
	{
		const auto atoms = result.AtomCount();
		if (atoms == 0 || mode >= result.ModeCount() || mode >= result.participation.size() / atoms ||
			!std::isfinite(fraction) || fraction <= 0.0 || fraction > 1.0)
			return 0;
		const auto first = result.participation.begin() + mode * atoms;
		std::vector<float> participation(first, first + atoms);
		if (std::any_of(participation.begin(), participation.end(), [](float value) {
			return !std::isfinite(value) || value < 0.0f;
		}))
			return 0;
		std::sort(participation.begin(), participation.end(), std::greater<float>());
		double cumulative = 0.0;
		for (std::size_t atom = 0; atom < atoms; ++atom)
		{
			cumulative += participation[atom];
			// Participation is float32; a decimal boundary may round just below the requested fraction.
			if (cumulative >= fraction || fraction - cumulative <=
				std::numeric_limits<float>::epsilon() * std::max(fraction, cumulative))
				return atom + 1;
		}
		return atoms;
	}

	std::vector<glm::dvec3> AnimatedPositions(const PhononAnalysisResult &result, std::size_t mode,
		double phaseRadians, double amplitudeAngstrom)
	{
		auto positions = result.positionsAngstrom;
		if (mode >= result.ModeCount() || result.modes[mode].isTranslation ||
			positions.size() != result.AtomCount() || !std::isfinite(phaseRadians) || !std::isfinite(amplitudeAngstrom))
			return positions;
		const auto displacements = PhysicalDisplacements(result, mode);
		const double longest = LongestDisplacement(displacements);
		if (longest > 0.0)
			for (std::size_t atom = 0; atom < displacements.size(); ++atom)
				positions[atom] += displacements[atom] * (amplitudeAngstrom * std::sin(phaseRadians) / longest);
		return positions;
	}
} // namespace DefectStudio
