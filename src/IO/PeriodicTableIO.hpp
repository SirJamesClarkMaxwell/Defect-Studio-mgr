#pragma once

#include <string>
#include <vector>

#include "Core/Diagnostics/StructuredError.hpp"
#include "Core/Utils/Path.hpp"

namespace DefectStudio
{
	struct PeriodicTableData
	{
		std::vector<std::string> elements;
		std::vector<std::string> lanthanides;
		std::vector<std::string> actinides;
	};

	class PeriodicTableIO
	{
	public:
		[[nodiscard]] static bool LoadFromFile(
			const Path &filePath,
			PeriodicTableData &outData,
			StructuredError &outError);
	};
} // namespace DefectStudio
