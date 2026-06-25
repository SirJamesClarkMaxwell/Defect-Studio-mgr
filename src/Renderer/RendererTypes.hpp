#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "Core/Utils/Path.hpp"
#include "Renderer/RendererSettings.hpp"

namespace DefectStudio
{
	struct RendererAtomData
	{
		std::string element;
		glm::vec3 cartesianPosition = glm::vec3(0.0f, 0.0f, 0.0f);
		glm::vec3 color = glm::vec3(0.7f, 0.7f, 0.7f);
		float radius = 0.35f;
		bool visible = true;
	};

	struct RendererColorGradient
	{
		glm::vec3 start = glm::vec3(0.6f, 0.6f, 0.6f);
		glm::vec3 finish = glm::vec3(0.9f, 0.9f, 0.9f);
	};

	struct RendererBondData
	{
		std::uint32_t firstAtomIndex = 0;
		std::uint32_t secondAtomIndex = 0;
		float radius = 0.09f;
		RendererColorGradient gradient;
	};

	struct RendererCellEdge
	{
		glm::vec3 start = glm::vec3(0.0f, 0.0f, 0.0f);
		glm::vec3 finish = glm::vec3(0.0f, 0.0f, 0.0f);
	};

	struct RendererStructureData
	{
		std::string name;
		Path sourcePath;
		std::vector<RendererAtomData> atoms;
		std::vector<RendererBondData> bonds;
		std::vector<RendererCellEdge> cellEdges;
		glm::mat3 lattice = glm::mat3(1.0f);
		glm::mat3 reciprocalLattice = glm::mat3(1.0f);
	};

	struct RendererViewSnapshot
	{
		glm::vec3 target = glm::vec3(0.0f);
		float distance = 1.0f;
		float yaw = 0.0f;
		float pitch = 0.0f;
		float roll = 0.0f;
		CameraProjection projection = CameraProjection::Perspective;
	};

	struct RendererViewStateChange
	{
		std::string description;
		RendererViewSnapshot before;
		RendererViewSnapshot after;
	};
} // namespace DefectStudio
