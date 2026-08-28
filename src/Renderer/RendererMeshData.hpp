#pragma once

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

namespace DefectStudio
{
	struct RendererStaticMeshData
	{
		std::vector<glm::vec3> positions;
		std::vector<glm::vec3> normals;
		std::vector<float> gradientT;
		std::vector<std::uint32_t> indices;
	};

	struct RendererPrimitiveMeshAssets
	{
		RendererStaticMeshData sphere;
		RendererStaticMeshData cylinder;
		RendererStaticMeshData cone; // SceneArrow Arrow3D head, see OpenGlRendererBackend::createConeMesh
	};
} // namespace DefectStudio
