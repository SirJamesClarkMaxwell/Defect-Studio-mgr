#pragma once

#include "Core/Utils/Path.hpp"
#include "Domain/Vibrations/BulkMode.hpp"

namespace DefectStudio::PhononModeIO
{
	// Failure leaves out unchanged; success clears error.
	[[nodiscard]] bool Load(const Path &directory, PhononModeField &out, std::string &error);
}
