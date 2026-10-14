#pragma once

#include "Domain/Vibrations/PhononModes.hpp"

namespace DefectStudio
{
	// At phase phi: real_i * sin(phi) + imag_i * cos(phi). Empty imag means a real field.
	struct VibrationField
	{
		std::string label;
		std::vector<std::string> symbols;
		std::vector<glm::dvec3> equilibriumAngstrom;
		std::vector<glm::dvec3> real, imag;
		std::vector<double> participation; // sums to 1; empty means uniform
	};

	// Largest semi-major axis over all atoms; one normalisation for the whole cycle.
	[[nodiscard]] double FieldCycleMaximum(const VibrationField &field);
	[[nodiscard]] std::vector<glm::dvec3> FieldDisplacements(const VibrationField &field, double phaseRadians,
		double amplitudeAngstrom);
	// Normalises the requested direction over ALL atoms before filtering; nullopt uses the real part.
	[[nodiscard]] std::vector<ModeArrow> FieldArrows(const VibrationField &field, std::optional<double> phaseRadians,
		double participationThreshold, double maxLengthAngstrom);
	[[nodiscard]] VibrationField DefectModeField(const PhononAnalysisResult &result, std::size_t mode,
		std::optional<std::size_t> partner = std::nullopt, double mixAngleRadians = 0.0);
	[[nodiscard]] std::optional<std::size_t> DegeneratePartner(const PhononAnalysisResult &result,
		std::size_t mode, double toleranceMeV = 0.5);
} // namespace DefectStudio
