#pragma once

#include <string>

#include "Core/Utils/Path.hpp"

namespace DefectStudio
{
	struct ConfigProfileEntry
	{
		std::string name;
		Path path;
	};
} // namespace DefectStudio
