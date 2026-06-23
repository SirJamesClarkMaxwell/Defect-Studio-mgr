#pragma once

#include <string>
#include <string_view>

#include "Core/Utils/Path.hpp"
#include "Renderer/RendererStartupDefinitions.hpp"

namespace DefectStudio
{
	class RendererStartupLayoutIO final
	{
	public:
		RendererStartupLayoutIO() = delete;

		static bool LoadFromFile(
			const Path &path,
			RendererStartupLayoutDefinition &outLayout,
			std::string &outError);

		static bool ParseYaml(
			std::string_view yamlText,
			const Path &baseDirectory,
			RendererStartupLayoutDefinition &outLayout,
			std::string &outError);
	};
} // namespace DefectStudio
