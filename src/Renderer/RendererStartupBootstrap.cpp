#include "Core/dspch.hpp"

#include "Renderer/RendererStartupBootstrap.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <iomanip>
#include <random>
#include <sstream>

#include <glm/geometric.hpp>

#include "Core/Logging/Logger.hpp"
#include "Renderer/RendererViewCamera.hpp"
#include "Renderer/Scene/SceneSystem.hpp"

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

	[[nodiscard]] static std::string GenerateRandomRendererWindowId()
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

	// Deterministic (same file -> same id, across restarts) instead of random, so persisted
	// per-window state (camera/selection/colors/...) and imgui.ini docking layout - both keyed by
	// windowId - actually mean something across a restart instead of being reshuffled every
	// launch. Falls back to the old random id when there's no source path to hash (synthetic/test
	// windows). Collisions between two live windows (the same file opened twice in one session)
	// are resolved by RendererLayer::AddWindow, the one place that can see what's already open.
	[[nodiscard]] static std::string GenerateRendererWindowId(const Path &sourcePath)
	{
		if (sourcePath.Empty())
			return GenerateRandomRendererWindowId();
		return ComputeDeterministicRendererWindowId(sourcePath);
	}

	std::string ComputeDeterministicRendererWindowId(const Path &sourcePath)
	{
		const std::size_t hash = std::hash<std::string>{}(sourcePath.String());
		std::ostringstream stream;
		stream << "ws-" << std::hex << std::setfill('0') << std::setw(16) << hash;
		return stream.str();
	}

	[[nodiscard]] static RendererWindowState BuildWindowFromStructure(
		const RendererStartupWindowDefinition &definition,
		RendererStructureData structure,
		glm::vec3 direction)
	{
		RendererWindowState window;
		window.windowId = GenerateRendererWindowId(definition.poscarPath);
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
		SceneSystem::SyncSceneWithStructure(window.sceneRegistry, window.structure);
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
