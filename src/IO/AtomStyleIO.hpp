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

		// Inverse of LoadFromFile/ParseYaml - same "vacancy" + "elements" shape, round-trips through
		// ParseYaml unchanged (see AtomStyleIOTests). Elements are written sorted by symbol so the
		// output is deterministic (unordered_map iteration order isn't) and diffs cleanly in git.
		static bool SaveToFile(
			const Path &path,
			const std::unordered_map<std::string, AtomRenderStyle> &styles,
			const VacancyRenderStyle &vacancyStyle,
			std::string &outError);
	};
} // namespace DefectStudio
