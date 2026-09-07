#pragma once

#include <optional>
#include <string>

#include "Core/JobSystem/JobSystemTypes.hpp"
#include "Core/Utils/Path.hpp"
#include "Domain/Crystal/CrystalStructure.hpp"

namespace DefectStudio
{
	// Orchestrates structure persistence off the main thread: creates the project structure
	// directory, writes the POSCAR file atomically (temp + rename), and returns the path to the
	// written file. On success GetResult() returns the POSCAR path after JobCompletedEvent.
	// On failure throws, producing JobFailedEvent instead.
	class AddStructureToProjectJob final : public IJob
	{
	public:
		AddStructureToProjectJob(
			CrystalStructure structure,
			std::string displayName,
			Path targetDirectory);

		[[nodiscard]] std::string GetName() const override;
		[[nodiscard]] std::string GetType() const override;
		void Execute(JobContext &context) override;

		[[nodiscard]] const std::optional<Path> &GetResult() const noexcept;

	private:
		CrystalStructure m_Structure;
		std::string m_DisplayName;
		Path m_TargetDirectory;
		std::optional<Path> m_WrittenPoscarPath;
	};
} // namespace DefectStudio
