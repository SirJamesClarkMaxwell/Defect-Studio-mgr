#include "Core/dspch.hpp"

#include "App/Jobs/AddStructureToProjectJob.hpp"

#include <fstream>
#include <utility>

#include "Core/JobSystem/JobContext.hpp"
#include "Core/Logging/Logger.hpp"
#include "Core/Utils/Path.hpp"
#include "Core/Utils/PathValidation.hpp"
#include "Domain/Crystal/StructureValidation.hpp"
#include "IO/PoscarWriter.hpp"

namespace DefectStudio
{
	namespace
	{
		// Written into the staging directory, so it survives the rename into the destination. Its
		// presence means "this directory is on disk but the domain never confirmed it registered" -
		// the coordinator deletes it right after Domain registration succeeds. A future startup scan
		// finds abandoned directories by this file alone, without needing anything to have been logged.
		constexpr const char *PendingRegistrationSentinel = ".pending_registration";
		constexpr const char *QuarantineMarker = ".quarantine";

		[[nodiscard]] StructuredError IoError(std::string userMessage, std::string technicalDetails, std::string suggestion)
		{
			return StructuredError(
				ErrorCategory::IO,
				Severity::Error,
				std::move(userMessage),
				std::move(technicalDetails),
				std::move(suggestion),
				"AddStructureToProjectJob");
		}

		bool WriteMarkerFile(const FilePath &path, const std::string &contents)
		{
			std::ofstream file(path);
			if (!file)
				return false;
			file << contents;
			return file.good();
		}
	} // namespace

	AddStructureToProjectJob::AddStructureToProjectJob(
		Uuid sessionId,
		Uuid attemptId,
		CrystalStructure structure,
		std::string sanitizedName,
		Path targetDirectory,
		Path authorizedRootSnapshot,
		Path scratchDirectory)
		: m_SessionId(sessionId),
		  m_AttemptId(attemptId),
		  m_Structure(std::move(structure)),
		  m_SanitizedName(std::move(sanitizedName)),
		  m_TargetDirectory(std::move(targetDirectory)),
		  m_AuthorizedRootSnapshot(std::move(authorizedRootSnapshot)),
		  m_ScratchDirectory(std::move(scratchDirectory))
	{
	}

	std::string AddStructureToProjectJob::GetName() const
	{
		return "Add Structure: " + m_SanitizedName;
	}

	std::string AddStructureToProjectJob::GetType() const
	{
		return "AddStructureToProjectJob";
	}

	void AddStructureToProjectJob::Execute(JobContext &context)
	{
		m_Outcome = run(context);
		if (!m_Outcome->HasValue())
			context.LogError(m_Outcome->Error().technicalDetails);
	}

	Result<Path> AddStructureToProjectJob::run(JobContext &context)
	{
		context.SetStage("validate");
		context.SetMessage("Validating target directory");
		context.SetProgress(0.0f, 4.0f);

		// 1. The target directory, re-checked here rather than trusted from the coordinator: this runs
		// later and closer to the write, which is the whole point of the second check.
		if (Result<void> symlinkFree = PathValidation::RejectSymlinkComponents(m_TargetDirectory.Native()); !symlinkFree)
			return symlinkFree.Error();

		std::error_code error;
		const FilePath canonicalTarget = std::filesystem::canonical(m_TargetDirectory.Native(), error);
		if (error)
			return IoError(
				"Target folder is not available",
				"canonical() failed for " + m_TargetDirectory.String() + ": " + error.message(),
				"Pick the destination folder again in the Project Tree.");

		if (!PathValidation::IsAncestor(m_AuthorizedRootSnapshot.Native(), canonicalTarget))
			return StructuredError(
				ErrorCategory::Validation,
				Severity::Error,
				"Target folder is outside the project",
				canonicalTarget.string() + " is not under " + m_AuthorizedRootSnapshot.String(),
				"Choose a folder inside the open project.",
				"AddStructureToProjectJob");

		// 1b. The name, re-validated independently of whatever the caller sanitized.
		Result<std::string> validatedName = PathValidation::ValidateAndSanitizeName(m_SanitizedName);
		if (!validatedName)
			return validatedName.Error();

		if (Result<void> validStructure = ValidateStructureForPersistence(m_Structure); !validStructure)
			return validStructure.Error();

		// 2-3. Destination, and the collision check. Note this check is best-effort against a racing
		// creator - the fail-if-exists rename in step 7 is what actually closes that window.
		const FilePath destinationPath = canonicalTarget / validatedName.Value();
		if (FileSystem::Exists(destinationPath))
			return IoError(
				"A structure with this name already exists",
				"Destination already exists: " + destinationPath.string(),
				"Use a different structure name.");

		// 4. Staging directory, required to be a sibling of the destination so the commit below is a
		// same-volume rename rather than a copy.
		context.SetStage("write-poscar");
		context.SetMessage("Writing POSCAR file");
		context.SetProgress(1.0f, 4.0f);

		const std::string stagingName = std::string("_poscar_staging_") + ToString(m_SessionId) + "_"
			+ ToString(m_AttemptId) + "_" + ToString(GenerateUuid());
		const FilePath stagingPath = canonicalTarget / stagingName;
		if (!std::filesystem::create_directory(stagingPath, error) || error)
			return IoError(
				"Could not prepare the structure folder",
				"create_directory failed for " + stagingPath.string() + ": " + error.message(),
				"Check that the project folder is writable, then retry.");

		// 5. The POSCAR itself. The Python bridge's scratch input lives OUTSIDE the staging directory,
		// so a failed cleanup of it can never end up committed inside the structure folder.
		const Path scratchInputPath = m_ScratchDirectory
			/ FilePath(ToString(m_SessionId) + "_" + ToString(m_AttemptId) + "_poscar_input.json");
		const FilePath poscarPath = stagingPath / "POSCAR";
		Result<void> written = PoscarWriter::Write(m_Structure, Path::FromResolved(poscarPath), scratchInputPath);

		std::error_code cleanupError;
		FileSystem::Remove(scratchInputPath.Native(), cleanupError); // Best-effort; never gates the result

		if (!written)
		{
			// 6. Nothing was committed: the destination was never created, so removing staging is the
			// complete rollback.
			FileSystem::RemoveAll(stagingPath, cleanupError);
			return written.Error();
		}

		// 5b. Sentinel, written before the commit so it is part of the directory the rename publishes.
		context.SetProgress(2.0f, 4.0f);
		if (!WriteMarkerFile(stagingPath / PendingRegistrationSentinel,
				"session=" + ToString(m_SessionId) + "\nattempt=" + ToString(m_AttemptId) + "\n"))
		{
			FileSystem::RemoveAll(stagingPath, cleanupError);
			return IoError(
				"Could not prepare the structure folder",
				"Failed to write " + std::string(PendingRegistrationSentinel) + " in " + stagingPath.string(),
				"Check that the project folder is writable, then retry.");
		}

		// 7. Commit. Fail-if-exists, never a replacing rename: if something created the destination
		// after step 3, that is a collision to report, not content to overwrite.
		context.SetStage("commit");
		context.SetMessage("Committing structure directory");
		context.SetProgress(3.0f, 4.0f);

		if (!FileSystem::RenameNoReplace(stagingPath, destinationPath, error))
		{
			// Staging holds the only copy of the written data, so it is quarantined, never deleted.
			WriteMarkerFile(
				canonicalTarget / (stagingName + QuarantineMarker),
				"attempt=" + ToString(m_AttemptId) + "\nreason=" + error.message() + "\nstaging=" + stagingPath.string() + "\n");

			if (error == std::errc::file_exists)
				return IoError(
					"A structure with this name already exists",
					"Destination appeared during the write: " + destinationPath.string(),
					"Use a different structure name.");

			return IoError(
				"Could not finish adding the structure",
				"Rename " + stagingPath.string() + " -> " + destinationPath.string() + " failed: " + error.message(),
				"The written data is kept next to the destination for recovery; see the log.");
		}

		context.SetProgress(4.0f, 4.0f);
		context.SetMessage("Structure written");
		return Path::FromResolved(destinationPath / "POSCAR");
	}

	const Uuid &AddStructureToProjectJob::GetSessionId() const noexcept
	{
		return m_SessionId;
	}

	const Uuid &AddStructureToProjectJob::GetAttemptId() const noexcept
	{
		return m_AttemptId;
	}

	const std::optional<Result<Path>> &AddStructureToProjectJob::GetOutcome() const noexcept
	{
		return m_Outcome;
	}

	const CrystalStructure &AddStructureToProjectJob::GetStructure() const noexcept
	{
		return m_Structure;
	}

	const std::string &AddStructureToProjectJob::GetDisplayName() const noexcept
	{
		return m_SanitizedName;
	}
} // namespace DefectStudio
