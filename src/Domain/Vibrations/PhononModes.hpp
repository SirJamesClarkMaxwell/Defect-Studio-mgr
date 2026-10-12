#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "Core/Utils/Path.hpp"

namespace DefectStudio
{
	// task/104b: one Gamma mode as scripts/python/examples/phonon_vibronic_analysis.py writes it to
	// modes.tsv (schema ds.phonon_vibronic.v1, docs/work/project/tasks/104-phonon-vibronic-script.md).
	struct PhononMode
	{
		int index = 0;
		double frequencyMeV = 0.0;       // signed hbar*omega; negative = imaginary
		std::string irrep;
		double effectiveAtomCount = 0.0; // dephonopy IPR: N for a uniform mode, 1 for one atom
		double beta = 0.0;               // atomCount / effectiveAtomCount
		double huangRhys = 0.0;          // S_k, 0 for translations
		double qAmuHalfAngstrom = 0.0;   // q_k = sum_i sqrt(m_i) e_ki . dR_i
		bool isTranslation = false;
	};

	struct PhononZplEv
	{
		std::optional<double> computed;
		std::optional<double> spinPurified;
		std::optional<double> experimental;
	};

	// The whole result directory in memory. The 162-atom reference case is ~1.3 MB of vectors, so
	// everything is loaded at once.
	// ponytail: eager load, read rows of eigenvectors.npy lazily if embedded cells (10^4+ atoms) arrive
	struct PhononAnalysisResult
	{
		Path directory;

		// structure.json - the force-constant supercell the modes belong to
		std::vector<std::string> symbols;
		std::vector<glm::dvec3> positionsAngstrom;
		glm::dmat3 cellAngstrom = glm::dmat3(1.0); // columns are lattice vectors a, b, c
		std::vector<double> massesAmu;

		std::vector<PhononMode> modes; // all 3N, translations included and flagged

		// Row-major float32 from the .npy files:
		//   eigenvectors[(mode * atomCount + atom) * 3 + xyz]  - normalised mass-weighted e_ki
		//   participation[mode * atomCount + atom]             - |e_ki|^2, each mode sums to 1
		std::vector<float> eigenvectors;
		std::vector<float> participation;

		// summary.json
		double sTotal = 0.0;
		double debyeWaller = 0.0;
		PhononZplEv zplEv;
		bool stable = true;
		std::vector<int> imaginaryModes;
		double sigmaMeV = 0.0;
		double gammaMeV = 0.0;
		double maxDisplacementAngstrom = 0.0;
		double deltaQAmuHalfAngstrom = 0.0;
		std::string approximation;

		// spectral_function.tsv
		std::vector<double> spectralEnergyMeV;
		std::vector<double> spectralDensityPerMeV;

		// pl.tsv; a column whose ZPL was not available is empty (it was all nan in the file)
		std::vector<double> plPhotonEnergyEv;
		std::vector<double> plComputed;
		std::vector<double> plSpinPurified;
		std::vector<double> plExperimental;

		[[nodiscard]] std::size_t AtomCount() const noexcept { return symbols.size(); }
		[[nodiscard]] std::size_t ModeCount() const noexcept { return modes.size(); }
	};

	struct ModeArrow
	{
		std::size_t atomIndex = 0;
		glm::dvec3 vectorAngstrom = glm::dvec3(0.0);
		double participation = 0.0;
	};

	// Arrows for one mode along the physical displacement u_i = e_ki / sqrt(m_i), scaled so the
	// longest arrow of the mode (over ALL atoms, before the threshold) is maxLengthAngstrom - the
	// threshold hides arrows, it never rescales the rest. Atoms with participation <
	// participationThreshold are left out. Empty for an out-of-range mode.
	[[nodiscard]] std::vector<ModeArrow> BuildModeArrows(const PhononAnalysisResult &result, std::size_t mode,
		double participationThreshold, double maxLengthAngstrom);

	// Smallest number of atoms whose participations, largest first, add up to at least `fraction`
	// (0..1] of the mode - "how many atoms take part". 0 for an out-of-range mode.
	[[nodiscard]] std::size_t AtomsCarryingFraction(const PhononAnalysisResult &result, std::size_t mode, double fraction);

	// Animation frame: R_i + amplitudeAngstrom * sin(phaseRadians) * u_i / max_j |u_j|, so the
	// atom with the largest physical displacement swings by exactly +-amplitudeAngstrom. Returns
	// positionsAngstrom unchanged for an out-of-range mode or a translation.
	[[nodiscard]] std::vector<glm::dvec3> AnimatedPositions(const PhononAnalysisResult &result, std::size_t mode,
		double phaseRadians, double amplitudeAngstrom);
} // namespace DefectStudio
