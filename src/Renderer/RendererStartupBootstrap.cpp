#include "Core/dspch.hpp"

#include "Renderer/RendererStartupBootstrap.hpp"

#include <chrono>

#include <glm/geometric.hpp>

#include "Core/Utils/Logger.hpp"
#include "Core/Utils/Time.hpp"
#include "Renderer/RendererPoscarLoader.hpp"
#include "Renderer/RendererPythonLoader.hpp"
#include "Renderer/RendererViewCamera.hpp"
#include "ScientificRuntime/Python/PymatgenBridge.hpp"

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

	[[nodiscard]] static Result<RendererStructureData> LoadStartupStructure(
		const RendererStartupWindowDefinition &definition,
		const AtomStyleTable &atomStyleTable,
		const ElementPropertiesTable &elementPropertiesTable,
		PymatgenBridge *pymatgenBridge,
		bool pythonAvailable)
	{
		const auto startTime = Time::NowSteady();
		if (pythonAvailable && pymatgenBridge != nullptr)
		{
			Result<RendererStructureData> loadedViaPython = LoadRendererStructureViaPython(
				definition.poscarPath,
				definition.structureName,
				*pymatgenBridge,
				atomStyleTable,
				elementPropertiesTable);
			if (loadedViaPython.HasValue())
			{
				const auto elapsedMilliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
					Time::NowSteady() - startTime).count();
				DS_LOG_DEBUG(
					"Renderer startup structure '{}' loaded via Python in {} ms",
					definition.structureName,
					elapsedMilliseconds);
				return loadedViaPython;
			}

			DS_LOG_WARN(
				"Renderer Python loader failed for {}; falling back to C++ POSCAR parser: {}",
				definition.poscarPath.String(),
				loadedViaPython.Error().technicalDetails);
		}

		Result<RendererStructureData> loadedViaPoscar = LoadRendererStructureFromPoscar(
			definition.poscarPath,
			definition.structureName,
			atomStyleTable,
			elementPropertiesTable);
		const auto elapsedMilliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
			Time::NowSteady() - startTime).count();
		DS_LOG_DEBUG(
			"Renderer startup structure '{}' loaded via C++ POSCAR in {} ms",
			definition.structureName,
			elapsedMilliseconds);
		return loadedViaPoscar;
	}

	[[nodiscard]] std::vector<RendererWindowState> BuildRendererStartupWindows(
		const std::vector<RendererStartupWindowDefinition> &windowDefinitions,
		const AtomStyleTable &atomStyleTable,
		const ElementPropertiesTable &elementPropertiesTable,
		PymatgenBridge *pymatgenBridge,
		bool pythonAvailable)
	{
		std::vector<RendererWindowState> windows;
		windows.reserve(windowDefinitions.size());
		if (pythonAvailable && pymatgenBridge != nullptr && !windowDefinitions.empty())
		{
			const auto startTime = Time::NowSteady();
			std::vector<Path> startupPaths;
			startupPaths.reserve(windowDefinitions.size());
			for (const RendererStartupWindowDefinition &definition : windowDefinitions)
				startupPaths.push_back(definition.poscarPath);

			Result<std::vector<PymatgenStructureData>> loadedViaPython = pymatgenBridge->LoadStructures(startupPaths);
			if (loadedViaPython.HasValue() && loadedViaPython->size() == windowDefinitions.size())
			{
				for (std::size_t index = 0; index < windowDefinitions.size(); ++index)
				{
					const RendererStartupWindowDefinition &definition = windowDefinitions[index];
					RendererStructureData structure = BuildRendererStructureFromPythonData(
						loadedViaPython->at(index),
						definition.poscarPath,
						definition.structureName,
						atomStyleTable,
						elementPropertiesTable);
					DS_LOG_INFO(
						"RendererPythonLoader: prepared '{}' via Python ({} atoms, {} bonds)",
						structure.name,
						structure.atoms.size(),
						structure.bonds.size());
					windows.push_back(BuildWindowFromStructure(definition, std::move(structure), definition.direction));
				}

				const auto elapsedMilliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
					Time::NowSteady() - startTime).count();
				DS_LOG_INFO(
					"Renderer startup batch loaded {} window(s) via Python in {} ms",
					windows.size(),
					elapsedMilliseconds);
				return windows;
			}

			if (!loadedViaPython.HasValue())
			{
				DS_LOG_WARN(
					"Renderer Python batch loader failed; falling back to C++ POSCAR parser: {}",
					loadedViaPython.Error().technicalDetails);
			}
			else
			{
				DS_LOG_WARN(
					"Renderer Python batch loader returned {} structures for {} windows; falling back to C++ POSCAR parser",
					loadedViaPython->size(),
					windowDefinitions.size());
			}
		}

		const auto fallbackStartTime = Time::NowSteady();
		for (const RendererStartupWindowDefinition &definition : windowDefinitions)
		{
			Result<RendererStructureData> loaded = LoadStartupStructure(
				definition,
				atomStyleTable,
				elementPropertiesTable,
				pymatgenBridge,
				false);
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

		const auto fallbackElapsedMilliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
			Time::NowSteady() - fallbackStartTime).count();
		DS_LOG_INFO(
			"Renderer startup loaded {} window(s) via C++ POSCAR fallback in {} ms",
			windows.size(),
			fallbackElapsedMilliseconds);
		return windows;
	}
} // namespace DefectStudio
