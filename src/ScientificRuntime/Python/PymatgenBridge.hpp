#pragma once

#include <vector>
#include <string>

#include <glm/glm.hpp>

#include "Core/Utils/Path.hpp"
#include "Core/Diagnostics/StructuredError.hpp"
#include "ScientificRuntime/Python/ScriptRunner.hpp"

namespace DefectStudio
{
	struct PymatgenRoundtripRequest
	{
		Path inputPoscarPath;
		Path outputPoscarPath;
	};

	struct PymatgenRoundtripResult
	{
		Path outputPoscarPath;
		std::string reducedFormula;
		int siteCount = 0;
	};

	struct PymatgenStructureSite
	{
		std::string element;
		glm::vec3 fractionalPosition = glm::vec3(0.0f);
		glm::vec3 cartesianPosition = glm::vec3(0.0f);
	};

	struct PymatgenStructureData
	{
		glm::mat3 lattice = glm::mat3(1.0f);
		std::vector<PymatgenStructureSite> sites;
		std::string reducedFormula;
	};

	class PymatgenBridge final
	{
	public:
		[[nodiscard]] Result<void> WarmUp();
		[[nodiscard]] bool IsWarmedUp() const noexcept;
		[[nodiscard]] Result<PymatgenStructureData> LoadStructure(const Path &filePath) const;
		[[nodiscard]] Result<std::vector<PymatgenStructureData>> LoadStructures(const std::vector<Path> &filePaths) const;
		[[nodiscard]] Result<PymatgenRoundtripResult> RoundtripPoscar(const PymatgenRoundtripRequest &request) const;

	private:
		[[nodiscard]] Result<std::vector<PymatgenStructureData>> LoadStructuresEmbedded(
			const std::vector<Path> &filePaths) const;
		[[nodiscard]] Result<std::vector<PymatgenStructureData>> LoadStructuresViaSubprocess(
			const std::vector<Path> &filePaths) const;

		ScriptRunner m_ScriptRunner;
		bool m_WarmedUp = false;
	};
} // namespace DefectStudio
