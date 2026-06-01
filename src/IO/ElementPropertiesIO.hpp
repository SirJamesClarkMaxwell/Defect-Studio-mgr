#pragma once

#include <string>
#include <string_view>
#include <unordered_map>

#include "Core/Utils/Path.hpp"
#include "Domain/Crystal/ElementProperties.hpp"

namespace DefectStudio
{
	class ElementPropertiesIO final
	{
	public:
		ElementPropertiesIO() = delete;

		static bool LoadFromFile(
			const Path &path,
			std::unordered_map<std::string, ElementProperties> &outEntries,
			std::string &outError);

		static bool ParseYaml(
			std::string_view yamlText,
			std::unordered_map<std::string, ElementProperties> &outEntries,
			std::string &outError);
	};
} // namespace DefectStudio
