#pragma once

#include <string>
#include <string_view>

#include "Core/Utils/Path.hpp"
#include "Renderer/RendererMeshData.hpp"

namespace DefectStudio
{
	class RendererMeshIO final
	{
	public:
		RendererMeshIO() = delete;

		static bool LoadObjFromFile(
			const Path &path,
			RendererStaticMeshData &outMesh,
			std::string &outError,
			bool generateGradientFromZ = false);

		static bool ParseObj(
			std::string_view objText,
			RendererStaticMeshData &outMesh,
			std::string &outError,
			bool generateGradientFromZ = false);
	};
} // namespace DefectStudio
