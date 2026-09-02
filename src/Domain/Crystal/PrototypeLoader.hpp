#pragma once

#include "Core/Diagnostics/StructuredError.hpp"
#include "Domain/Crystal/PrototypeDefinition.hpp"

namespace DefectStudio
{
	class PrototypeLoader
	{
	public:
		static Result<PrototypesAndMaterials> LoadBuiltIn();
		static Result<PrototypesAndMaterials> LoadFromFile(const std::string &path);
	};
}
