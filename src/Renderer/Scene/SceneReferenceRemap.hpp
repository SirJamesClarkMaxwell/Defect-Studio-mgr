#pragma once

#include <cstddef>
#include <vector>

namespace DefectStudio
{
	struct RendererWindowState;

	// Call before rebuilding window.structure: its positions/elements are the pre-delete context.
	// Removed indices must be unique and ascending; vacancy indices have the same order.
	void RemapSceneAtomReferencesAfterAtomRemoval(RendererWindowState &window,
		const std::vector<std::size_t> &removed, const std::vector<std::size_t> &vacancyIndices);
	void RemapSceneVacancyReferencesAfterVacancyRemoval(RendererWindowState &window,
		const std::vector<std::size_t> &removed);
}
