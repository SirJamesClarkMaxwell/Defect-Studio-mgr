#pragma once

#include <string>
#include <string_view>

#include "Core/Utils/Path.hpp"

namespace DefectStudio
{
	class TextFileIO final
	{
	public:
		TextFileIO() = delete;

		[[nodiscard]] static bool Save(const Path &path, std::string_view text, std::string &error);
		[[nodiscard]] static bool Load(const Path &path, std::string &text, std::string &error);
	};
} // namespace DefectStudio
