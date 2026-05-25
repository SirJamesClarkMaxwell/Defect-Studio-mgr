#include "Core/dspch.hpp"

#include "Renderer/RendererQuickTestBootstrap.hpp"

#include <array>

#include <glm/geometric.hpp>

#include "Core/Utils/Logger.hpp"
#include "Renderer/RendererPoscarLoader.hpp"
#include "Renderer/RendererViewCamera.hpp"

namespace DefectStudio
{
	[[nodiscard]] static Path QuickPoscarDirectory(const Path &assetsDirectory)
	{
		return assetsDirectory / Path("quicktest") / Path("poscar");
	}

	[[nodiscard]] static Path ResolveExistingQuickPoscar(
		const Path &poscarDirectory,
		const std::array<Path, 2> &candidates)
	{
		for (const Path &candidate : candidates)
		{
			const Path fullPath = poscarDirectory / candidate;
			if (FileSystem::Exists(fullPath.Native()))
				return fullPath;
		}

		return poscarDirectory / candidates[0];
	}

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
		std::string title,
		RendererStructureData structure,
		const glm::vec3 &direction,
		float distanceMultiplier,
		std::size_t hideStep)
	{
		RendererWindowState window;
		window.title = std::move(title);
		window.structure = std::move(structure);
		window.camera = CreateUnique<RendererViewCamera>();

		glm::vec3 minimum(1e6f, 1e6f, 1e6f);
		glm::vec3 maximum(-1e6f, -1e6f, -1e6f);
		for (const RendererAtomData &atom : window.structure.atoms)
		{
			minimum = glm::min(minimum, atom.cartesianPosition);
			maximum = glm::max(maximum, atom.cartesianPosition);
		}
		window.camera->FocusBounds(minimum, maximum);
		window.camera->SetFromDirection(direction);
		window.camera->SetDistance(window.camera->Distance() * distanceMultiplier);
		HideAtomPattern(window.structure, hideStep);
		return window;
	}

	[[nodiscard]] std::vector<RendererWindowState> BuildRendererQuickTestWindows(const Path &assetsDirectory)
	{
		std::vector<RendererWindowState> windows;
		const Path poscarDirectory = QuickPoscarDirectory(assetsDirectory);
		const Path hbnPath = ResolveExistingQuickPoscar(
			poscarDirectory,
			{Path("hBN.vasp"), Path("hBN")});
		const std::array<Path, 3> files = {
			poscarDirectory / Path("NaCl.vasp"),
			hbnPath,
			poscarDirectory / Path("CdIn2S4.vasp")};
		const std::array<std::string, 3> names = {"NaCl", "hBN", "CdIn2S4"};
		const std::array<glm::vec3, 3> directions = {
			glm::normalize(glm::vec3(1.0f, 1.0f, 0.8f)),
			glm::normalize(glm::vec3(-1.0f, 1.2f, 0.7f)),
			glm::normalize(glm::vec3(1.0f, 0.9f, 1.2f))};
		const std::array<float, 3> distanceScale = {1.25f, 1.15f, 1.75f};
		const std::array<std::size_t, 3> hideSteps = {0, 7, 5};

		for (std::size_t index = 0; index < files.size(); ++index)
		{
			Result<RendererStructureData> loaded = LoadRendererStructureFromPoscar(files[index], names[index]);
			if (!loaded.HasValue())
			{
				DS_LOG_ERROR(
					"Renderer quick-test bootstrap failed for {}: {}",
					files[index].String(),
					loaded.Error().technicalDetails);
				continue;
			}

			RendererWindowState window = BuildWindowFromStructure(
				"T06 | " + names[index],
				std::move(loaded.Value()),
				directions[index],
				distanceScale[index],
				hideSteps[index]);
			windows.push_back(std::move(window));
		}

		return windows;
	}
} // namespace DefectStudio
