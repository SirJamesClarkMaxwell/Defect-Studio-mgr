#pragma once

#include "IO/PhononPanelSettingsIO.hpp"

namespace DefectStudio::PhononSettingsDetail
{
	// Keep v1 validation and path rules shared with the per-case v2 records.
	bool ParseFlat(const std::string &text, const Path &projectDirectory,
		PhononPanelSettings &settings, std::vector<std::string> &warnings, std::string &error);
	std::string SerializeFlat(const PhononPanelSettings &settings, const Path &projectDirectory);
}
