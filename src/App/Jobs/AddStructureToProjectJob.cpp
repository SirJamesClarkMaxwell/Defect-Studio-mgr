#include "Core/dspch.hpp"

#include "App/Jobs/AddStructureToProjectJob.hpp"

#include <stdexcept>
#include <utility>

#include "Core/JobSystem/JobContext.hpp"
#include "Core/Utils/Path.hpp"
#include "IO/PoscarWriter.hpp"

namespace DefectStudio
{
	AddStructureToProjectJob::AddStructureToProjectJob(
		CrystalStructure structure,
		std::string displayName,
		Path targetDirectory)
		: m_Structure(std::move(structure)),
		  m_DisplayName(std::move(displayName)),
		  m_TargetDirectory(std::move(targetDirectory))
	{
	}

	std::string AddStructureToProjectJob::GetName() const
	{
		return "Add Structure: " + m_DisplayName;
	}

	std::string AddStructureToProjectJob::GetType() const
	{
		return "AddStructureToProjectJob";
	}

	void AddStructureToProjectJob::Execute(JobContext &context)
	{
		context.SetStage("create-directory");
		context.SetMessage("Creating structure directory");
		context.SetProgress(0.0f, 2.0f);

		// Create project/structures/<displayName>/ directory
		const Path structureDir = m_TargetDirectory / m_DisplayName;
		if (!FileSystem::CreateDirectories(structureDir.Native()))
			throw std::runtime_error("Failed to create structure directory at " + structureDir.String());

		context.SetProgress(1.0f, 2.0f);
		context.SetStage("write-poscar");
		context.SetMessage("Writing POSCAR file");

		// Write POSCAR file atomically (temp + rename)
		const Path poscarPath = structureDir / "POSCAR";
		Result<void> writeResult = PoscarWriter::Write(m_Structure, poscarPath);
		if (!writeResult)
			throw std::runtime_error("POSCAR write failed: " + writeResult.Error().technicalDetails);

		m_WrittenPoscarPath = poscarPath;
		context.SetProgress(2.0f, 2.0f);
		context.SetMessage("Structure added successfully");
	}

	const std::optional<Path> &AddStructureToProjectJob::GetResult() const noexcept
	{
		return m_WrittenPoscarPath;
	}

	const CrystalStructure &AddStructureToProjectJob::GetStructure() const noexcept
	{
		return m_Structure;
	}

	const std::string &AddStructureToProjectJob::GetDisplayName() const noexcept
	{
		return m_DisplayName;
	}
} // namespace DefectStudio
