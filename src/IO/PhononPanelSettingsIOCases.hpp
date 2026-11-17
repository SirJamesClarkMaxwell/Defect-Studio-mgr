#pragma once

#include "IO/PhononPanelSettingsIO.hpp"

namespace YAML { class Node; }

namespace DefectStudio::PhononSettingsDetail
{
	// Keep v1 validation and path rules shared with the per-case v2 records.
	bool ParseFlat(const std::string &text, const Path &projectDirectory,
		PhononPanelSettings &settings, std::vector<std::string> &warnings, std::string &error);
	std::string SerializeFlat(const PhononPanelSettings &settings, const Path &projectDirectory);
	std::string StoredPath(const Path &path, const Path &projectDirectory);
	Path ReadPath(const YAML::Node &node, const Path &projectDirectory);
	void ReadExperimental(const YAML::Node &node, const Path &project, PhononCase &item, std::vector<std::string> &warnings);
	void WriteExperimental(YAML::Node &node, const Path &project, const PhononCase &item);
}
