#pragma once

#include <vector>

#include "Renderer/RendererStartupDefinitions.hpp"
#include "Renderer/RendererWindowState.hpp"

namespace DefectStudio
{
	struct RendererStartupWindowInput
	{
		RendererStartupWindowDefinition definition;
		RendererStructureData structure;
	};

	[[nodiscard]] std::vector<RendererWindowState> BuildRendererStartupWindows(
		std::vector<RendererStartupWindowInput> windowInputs);

	// Same hash formula BuildRendererStartupWindows uses internally to give a window opened from
	// `sourcePath` a stable id (same file -> same id, across restarts and re-opens) - exposed so
	// EditorLayer's project-state restore can predict a persisted window's id before it's actually
	// reopened, instead of fuzzy-matching by path once it appears.
	[[nodiscard]] std::string ComputeDeterministicRendererWindowId(const Path &sourcePath);
}
