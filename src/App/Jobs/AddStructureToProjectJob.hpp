#pragma once

#include <optional>
#include <string>

#include "Core/Diagnostics/StructuredError.hpp"
#include "Core/JobSystem/JobSystemTypes.hpp"
#include "Core/Utils/Path.hpp"
#include "Core/Utils/Uuid.hpp"
#include "Domain/Crystal/CrystalStructure.hpp"

namespace DefectStudio
{
	// Writes one structure into a project directory, off the main thread.
	//
	// Staging-directory contract - this is the ONLY write path, there is no direct-write variant:
	// everything is written into a sibling staging directory under the same target directory (so the
	// commit is guaranteed same-volume), and that directory is then renamed onto the destination with
	// a fail-if-exists primitive. Consequence: the destination either does not exist, or exists
	// complete. A reader can never catch a half-written POSCAR.
	//
	// This guarantees visibility atomicity, NOT crash durability - nothing is fsync'd before the
	// rename, so a crash before the OS flushes can lose the write entirely. The user retries.
	//
	// The job re-validates everything it was handed (target directory, name, structure) instead of
	// trusting the coordinator: it is its own security boundary, and it runs later than the caller's
	// checks, closer to the actual write.
	//
	// Never throws for an expected failure. The outcome - success path or StructuredError - is in
	// GetOutcome() after JobCompletedEvent, so the coordinator resolves both the same way and an
	// attempt can never go missing.
	class AddStructureToProjectJob final : public IJob
	{
	public:
		AddStructureToProjectJob(
			Uuid sessionId,
			Uuid attemptId,
			CrystalStructure structure,
			std::string sanitizedName,
			Path targetDirectory,
			Path authorizedRootSnapshot,
			Path scratchDirectory);

		[[nodiscard]] std::string GetName() const override;
		[[nodiscard]] std::string GetType() const override;
		void Execute(JobContext &context) override;

		[[nodiscard]] const Uuid &GetSessionId() const noexcept;
		[[nodiscard]] const Uuid &GetAttemptId() const noexcept;
		// Success carries the written POSCAR path; failure carries the reason. Empty only if the job
		// never ran (submission failed or it was cancelled before Execute).
		[[nodiscard]] const std::optional<Result<Path>> &GetOutcome() const noexcept;
		[[nodiscard]] const CrystalStructure &GetStructure() const noexcept;
		[[nodiscard]] const std::string &GetDisplayName() const noexcept;

	private:
		[[nodiscard]] Result<Path> run(JobContext &context);

		Uuid m_SessionId;
		Uuid m_AttemptId;
		CrystalStructure m_Structure;
		std::string m_SanitizedName;
		Path m_TargetDirectory;
		Path m_AuthorizedRootSnapshot;
		Path m_ScratchDirectory;
		std::optional<Result<Path>> m_Outcome;
	};
} // namespace DefectStudio
