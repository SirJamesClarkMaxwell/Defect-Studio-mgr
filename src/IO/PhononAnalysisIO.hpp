#pragma once

#include <string>

#include "Core/Utils/Path.hpp"
#include "Domain/Vibrations/PhononModes.hpp"

namespace DefectStudio
{
	// task/104b: reads the result directory of scripts/python/examples/phonon_vibronic_analysis.py
	// (schema ds.phonon_vibronic.v1): summary.json, structure.json, modes.tsv, eigenvectors.npy,
	// participation.npy, spectral_function.tsv, pl.tsv.
	class PhononAnalysisIO
	{
	public:
		static constexpr const char *kSchema = "ds.phonon_vibronic.v1";

		// False (outError set, outResult untouched) when a file is missing or unreadable, the schema
		// is not kSchema, a .npy is not little-endian float32 C-order with the expected shape
		// ((modes, atoms, 3) and (modes, atoms)), modes.tsv does not have 3 * atoms rows, or
		// structure.json arrays differ in length. Spectra files are optional: a missing
		// spectral_function.tsv or pl.tsv leaves those vectors empty. A pl.tsv column that is all
		// nan is left empty.
		[[nodiscard]] static bool Load(const Path &directory, PhononAnalysisResult &outResult, std::string &outError);
	};
} // namespace DefectStudio
