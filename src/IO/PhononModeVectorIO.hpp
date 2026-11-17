#pragma once

#include "Domain/Vibrations/PhononModes.hpp"

namespace DefectStudio::PhononModeVectorIO
{
	// Shared strict NPY reader used by full analyses and single-mode outputs; throws on invalid data.
	[[nodiscard]] std::vector<float> ReadNpy(const Path &path, const std::vector<std::size_t> &expectedShape);
	// Failure leaves out unchanged and supplies an error.
	[[nodiscard]] bool Load(const Path &directory, PhononModeVector &out, std::string &error);
	[[nodiscard]] bool EmbeddedDirectory(const Path &dataset, Path &out, std::string &error);
}
