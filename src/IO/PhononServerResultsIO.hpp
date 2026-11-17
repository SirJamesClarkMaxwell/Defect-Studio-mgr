#pragma once

#include <array>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "Core/Utils/Path.hpp"

namespace DefectStudio
{
	// task/113a: Huang-Rhys results computed on the cluster by dephonopy (`calc_hr_factors`, the
	// user's PL-spectra/scripts/hr_factors.py), read in place from a mounted drive:
	//   <system>/spectral-function/<A>x<B>x<C>/<variant>/hr_factors.dat   (+ freq_mev.dat, irreps.dat, hr_factors.log)
	//   <system>/embedded/<A>x<B>x<C>/{iprs.dat, POSCAR, defect.yaml, modes.h5}   (optional)
	// <variant> is "<method>" or "<method>_cutoff<number>A", e.g. "displacements_cutoff4A".
	struct ServerHrResult
	{
		Path hrDirectory;     // .../spectral-function/12x12x1/displacements
		Path systemDirectory; // parent of spectral-function
		std::array<int, 3> supercell{};
		std::string variant;  // directory name, e.g. "displacements_cutoff4A"
		std::string method;   // variant without the "_cutoff..." suffix
		std::optional<double> cutoffAngstrom; // from the variant name; nullopt = automatic
	};

	// Every hr_factors.dat at the layout above under root. root may be anything from an ancestor of
	// <system> down to the hr directory itself. Directories named .git, .venv, __pycache__,
	// embedded, dispersion, lineshape and run_computations are not descended into; symlinked
	// directories are skipped. A size directory that is not <A>x<B>x<C> with positive integers is
	// ignored. Sorted by system directory, then variant, then A*B*C, then supercell. Returns what was
	// found so far when cancelled() becomes true.
	[[nodiscard]] std::vector<ServerHrResult> FindServerHrResults(const Path &root, const std::function<bool()> &cancelled);
} // namespace DefectStudio
