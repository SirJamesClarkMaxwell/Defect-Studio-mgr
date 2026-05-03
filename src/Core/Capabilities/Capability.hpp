#pragma once

#include <string>

namespace DefectStudio
{
	enum class CapabilityCategory
	{
		BuildTime,
		RuntimeDetected,
		Policy
	};

	struct CapabilityEntry
	{
		std::string name;
		CapabilityCategory category = CapabilityCategory::RuntimeDetected;
		bool available = false;
		std::string description;
	};

} // namespace DefectStudio
