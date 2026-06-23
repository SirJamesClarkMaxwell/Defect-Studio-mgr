#pragma once

#include <string>
#include <string_view>
#include <unordered_map>

#include "Core/Utils/Path.hpp"
#include "Renderer/AtomStyleTable.hpp"

namespace DefectStudio
{
	class AtomStyleIO final
	{
	public:
		AtomStyleIO() = delete;

		static bool LoadFromFile(
			const Path &path,
			std::unordered_map<std::string, AtomRenderStyle> &outStyles,
			VacancyRenderStyle &outVacancyStyle,
			std::string &outError);

		static bool ParseYaml(
			std::string_view yamlText,
			std::unordered_map<std::string, AtomRenderStyle> &outStyles,
			VacancyRenderStyle &outVacancyStyle,
			std::string &outError);
	};
} // namespace DefectStudio
