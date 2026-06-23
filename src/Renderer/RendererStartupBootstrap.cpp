#include "Core/dspch.hpp"

#include "Renderer/RendererStartupBootstrap.hpp"

#include <glm/geometric.hpp>

#include "Core/Utils/Logger.hpp"
#include "Renderer/RendererPoscarLoader.hpp"
#include "Renderer/RendererViewCamera.hpp"

namespace DefectStudio
{
	static void HideAtomPattern(RendererStructureData &structure, std::size_t step)
	{
		if (step == 0)
			return;
		for (std::size_t index = 0; index < structure.atoms.size(); ++index)
		{
			if (index % step == 0)
				structure.atoms[index].visible = false;
		}
	}

	[[nodiscard]] static RendererWindowState BuildWindowFromStructure(
		const RendererStartupWindowDefinition &definition,
		RendererStructureData structure,
		glm::vec3 direction)
	{
		RendererWindowState window;
		window.title = definition.title;
		window.structure = std::move(structure);
		window.camera = CreateUnique<RendererViewCamera>();

		glm::vec3 minimum(1e6f, 1e6f, 1e6f);
		glm::vec3 maximum(-1e6f, -1e6f, -1e6f);
		for (const RendererAtomData &atom : window.structure.atoms)
		{
			minimum = glm::min(minimum, atom.cartesianPosition);
			maximum = glm::max(maximum, atom.cartesianPosition);
		}
		if (glm::length(direction) <= 0.0001f)
			direction = glm::vec3(1.0f, 1.0f, 1.0f);
		direction = glm::normalize(direction);
		window.camera->FocusBounds(minimum, maximum);
		window.camera->SetFromDirection(direction);
		window.camera->SetDistance(window.camera->Distance() * definition.distanceMultiplier);
		HideAtomPattern(window.structure, definition.hideStep);
		return window;
	}

	[[nodiscard]] std::vector<RendererWindowState> BuildRendererStartupWindows(
		const std::vector<RendererStartupWindowDefinition> &windowDefinitions,
		const AtomStyleTable &atomStyleTable,
		const ElementPropertiesTable &elementPropertiesTable)
	{
		std::vector<RendererWindowState> windows;
		windows.reserve(windowDefinitions.size());
		for (const RendererStartupWindowDefinition &definition : windowDefinitions)
		{
			Result<RendererStructureData> loaded = LoadRendererStructureFromPoscar(
				definition.poscarPath,
				definition.structureName,
				atomStyleTable,
				elementPropertiesTable);
			if (!loaded.HasValue())
			{
				DS_LOG_ERROR(
					"Renderer quick-test bootstrap failed for {}: {}",
					definition.poscarPath.String(),
					loaded.Error().technicalDetails);
				continue;
			}

			RendererWindowState window = BuildWindowFromStructure(definition, std::move(loaded.Value()), definition.direction);
			windows.push_back(std::move(window));
		}

		return windows;
	}
} // namespace DefectStudio
