#include "Core/dspch.hpp"

#include "Renderer/RendererStartupBootstrap.hpp"

#include <array>
#include <cstdint>
#include <iomanip>
#include <random>
#include <sstream>

#include <glm/geometric.hpp>

#include "Core/Logging/Logger.hpp"
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

	[[nodiscard]] static std::string GenerateRendererWindowId()
	{
		std::random_device randomDevice;
		std::uniform_int_distribution<std::uint32_t> distribution(0, 255);
		std::array<std::uint8_t, 16> bytes{};
		for (std::uint8_t &byte : bytes)
			byte = static_cast<std::uint8_t>(distribution(randomDevice));

		bytes[6] = static_cast<std::uint8_t>((bytes[6] & 0x0f) | 0x40);
		bytes[8] = static_cast<std::uint8_t>((bytes[8] & 0x3f) | 0x80);

		std::ostringstream stream;
		stream << std::hex << std::setfill('0');
		for (std::size_t index = 0; index < bytes.size(); ++index)
		{
			if (index == 4 || index == 6 || index == 8 || index == 10)
				stream << '-';
			stream << std::setw(2) << static_cast<int>(bytes[index]);
		}
		return stream.str();
	}

	[[nodiscard]] static RendererWindowState BuildWindowFromStructure(
		const RendererStartupWindowDefinition &definition,
		RendererStructureData structure,
		glm::vec3 direction)
	{
		RendererWindowState window;
		window.windowId = GenerateRendererWindowId();
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
		std::vector<RendererStartupWindowInput> windowInputs)
	{
		std::vector<RendererWindowState> windows;
		windows.reserve(windowInputs.size());

		for (RendererStartupWindowInput &input : windowInputs)
		{
			RendererWindowState window = BuildWindowFromStructure(
				input.definition,
				std::move(input.structure),
				input.definition.direction);
			windows.push_back(std::move(window));
		}

		DS_LOG_INFO("Renderer startup built {} window(s) from prepared structure data", windows.size());
		return windows;
	}
} // namespace DefectStudio
