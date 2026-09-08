#include "Core/dspch.hpp"

#include "App/StructureLifecycleCoordinator.hpp"

#include <functional>
#include <utility>

#include "Core/Domain/StructureLifecycleEvents.hpp"
#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Core/Logging/Logger.hpp"
#include "Core/Utils/PathValidation.hpp"
#include "Domain/Crystal/StructureValidation.hpp"
#include "Domain/DomainLayer.hpp"

namespace DefectStudio
{
	namespace
	{
		constexpr const char *PendingRegistrationSentinel = ".pending_registration";

		[[nodiscard]] StructuredError WorkflowError(std::string userMessage, std::string technicalDetails, std::string suggestion)
		{
			return StructuredError(
				ErrorCategory::Runtime,
				Severity::Error,
				std::move(userMessage),
				std::move(technicalDetails),
				std::move(suggestion),
				"StructureLifecycleCoordinator");
		}
	} // namespace

	StructureLifecycleCoordinator::StructureLifecycleCoordinator(
		Ref<EventBus> eventBus,
		WeakRef<JobSystem> jobSystem,
		WeakRef<DomainLayer> domainLayer,
		Ref<CreationSessionRegistry> sessionRegistry,
		Path scratchDirectory)
		: m_EventBus(std::move(eventBus)),
		  m_JobSystem(std::move(jobSystem)),
		  m_DomainLayer(std::move(domainLayer)),
		  m_SessionRegistry(std::move(sessionRegistry)),
		  m_ScratchDirectory(std::move(scratchDirectory))
	{
		if (m_EventBus == nullptr)
			return;

		AddSubscription(m_EventBus->Subscribe<ProjectEvents::ProjectRootsChanged>(
			std::bind_front(&StructureLifecycleCoordinator::onProjectRootsChanged, this)));
		AddSubscription(m_EventBus->Subscribe<DomainEvents::SessionReadyForStructureHub>(
			std::bind_front(&StructureLifecycleCoordinator::onSessionReadyForStructureHub, this)));
		AddSubscription(m_EventBus->Subscribe<DomainEvents::AddStructureToProjectRequested>(
			std::bind_front(&StructureLifecycleCoordinator::onAddStructureToProjectRequested, this)));
		AddSubscription(m_EventBus->Subscribe<DomainEvents::RendererTabClosed>(
			std::bind_front(&StructureLifecycleCoordinator::onRendererTabClosed, this)));
		AddSubscription(m_EventBus->Subscribe<JobCompletedEvent>(
			std::bind_front(&StructureLifecycleCoordinator::onJobCompleted, this)));
		AddSubscription(m_EventBus->Subscribe<JobFailedEvent>(
			std::bind_front(&StructureLifecycleCoordinator::onJobFailed, this)));
	}

	StructureLifecycleCoordinator::~StructureLifecycleCoordinator()
	{
		ClearSubscriptions();
	}

	void StructureLifecycleCoordinator::onProjectRootsChanged(const ProjectEvents::ProjectRootsChanged &event)
	{
		m_ProjectRoots.clear();
		for (const Path &root : event.roots)
		{
			if (root.Empty())
				continue;

			// A root that is itself a symlink is rejected once, here, rather than re-litigated on
			// every later check - supporting symlinked project roots is explicitly future work.
			if (Result<void> symlinkFree = PathValidation::RejectSymlinkComponents(root.Native()); !symlinkFree)
			{
				DS_LOG_WARN("Structure Lifecycle: ignoring project root '{}': {}",
					root.String(), symlinkFree.Error().technicalDetails);
				continue;
			}

			std::error_code error;
			const FilePath canonical = std::filesystem::canonical(root.Native(), error);
			if (error)
			{
				DS_LOG_WARN("Structure Lifecycle: ignoring project root '{}': {}", root.String(), error.message());
				continue;
			}
			m_ProjectRoots.push_back(Path::FromResolved(canonical));
		}
	}

	void StructureLifecycleCoordinator::onSessionReadyForStructureHub(
		const DomainEvents::SessionReadyForStructureHub &event)
	{
		Ref<CreationSession> session = m_SessionRegistry->Find(event.sessionId).lock();
		if (session == nullptr)
		{
			DS_LOG_WARN("Structure Lifecycle: hand-off for unknown session {}", ToString(event.sessionId));
			return;
		}

		session->targetDirectory = Path::FromResolved(event.targetDirectory);
		session->state = CreationSessionState::Ready; // Tab is open; no attempt exists yet
		session->lastModifiedAt = Time::Now();
	}

	Result<std::pair<Path, Path>> StructureLifecycleCoordinator::authorizeTarget(const FilePath &targetDirectory) const
	{
		if (targetDirectory.empty())
			return WorkflowError(
				"No target folder selected",
				"AddStructureToProjectRequested carried an empty targetDirectory",
				"Click the destination folder in the Project Tree first.");

		if (Result<void> symlinkFree = PathValidation::RejectSymlinkComponents(targetDirectory); !symlinkFree)
			return symlinkFree.Error();

		std::error_code error;
		const FilePath canonical = std::filesystem::canonical(targetDirectory, error);
		if (error)
			return WorkflowError(
				"Target folder is not available",
				"canonical() failed for " + targetDirectory.string() + ": " + error.message(),
				"Pick the destination folder again in the Project Tree.");

		for (const Path &root : m_ProjectRoots)
		{
			if (PathValidation::IsAncestor(root.Native(), canonical))
				return std::pair<Path, Path>{Path::FromResolved(canonical), root};
		}

		return WorkflowError(
			"Target folder is outside the project",
			canonical.string() + " is not under any registered project root",
			"Choose a folder inside one of the open project roots.");
	}

	void StructureLifecycleCoordinator::publishFailure(const Uuid &sessionId, const Uuid &attemptId, StructuredError error)
	{
		DomainEvents::ProjectStructureAddFailed failureEvent;
		failureEvent.sessionId = sessionId;
		failureEvent.attemptId = attemptId;
		failureEvent.error = std::move(error);
		m_EventBus->Publish(failureEvent);
	}

	void StructureLifecycleCoordinator::onAddStructureToProjectRequested(
		const DomainEvents::AddStructureToProjectRequested &event)
	{
		Ref<CreationSession> session = m_SessionRegistry->Find(event.sessionId).lock();
		if (session == nullptr)
		{
			DS_LOG_WARN("Structure Lifecycle: add requested for unknown session {}", ToString(event.sessionId));
			publishFailure(event.sessionId, Uuid{}, WorkflowError(
				"This structure draft is no longer open",
				"No CreationSession for " + ToString(event.sessionId),
				"Start a new structure and try again."));
			return;
		}

		// Single-in-flight enforcement, and the Closing gate, both live here rather than in the UI.
		if (session->activeAttemptId.has_value())
		{
			publishFailure(event.sessionId, Uuid{}, WorkflowError(
				"This structure is already being added",
				"Session " + ToString(event.sessionId) + " already has attempt "
					+ ToString(*session->activeAttemptId) + " in flight",
				"Wait for the current attempt to finish."));
			return;
		}

		if (session->state == CreationSessionState::Closing)
		{
			publishFailure(event.sessionId, Uuid{}, WorkflowError(
				"This structure draft is closing",
				"Session " + ToString(event.sessionId) + " is in Closing state",
				"Start a new structure and try again."));
			return;
		}

		Result<std::string> sanitizedName = PathValidation::ValidateAndSanitizeName(event.displayName);
		if (!sanitizedName)
		{
			session->lastError = sanitizedName.Error();
			publishFailure(event.sessionId, Uuid{}, sanitizedName.Error());
			return;
		}

		Result<std::pair<Path, Path>> authorized = authorizeTarget(event.targetDirectory);
		if (!authorized)
		{
			session->lastError = authorized.Error();
			publishFailure(event.sessionId, Uuid{}, authorized.Error());
			return;
		}

		if (Result<void> validStructure = ValidateStructureForPersistence(event.structure); !validStructure)
		{
			session->lastError = validStructure.Error();
			publishFailure(event.sessionId, Uuid{}, validStructure.Error());
			return;
		}

		Ref<JobSystem> jobSystem = m_JobSystem.lock();
		if (jobSystem == nullptr)
		{
			StructuredError error = WorkflowError(
				"Cannot add the structure right now",
				"JobSystem unavailable while adding '" + event.displayName + "'",
				"Restart the application and try again.");
			session->lastError = error;
			session->state = CreationSessionState::Failed;
			publishFailure(event.sessionId, Uuid{}, std::move(error));
			return;
		}

		// Attempt identity is created here, in the same step as the check-and-set above: the UI never
		// supplies it, so it cannot forge or reuse one.
		const Uuid attemptId = GenerateUuid();
		session->activeAttemptId = attemptId;
		session->state = CreationSessionState::Submitted;
		session->lastError.reset();
		session->lastModifiedAt = Time::Now();

		Ref<AddStructureToProjectJob> job = CreateRef<AddStructureToProjectJob>(
			event.sessionId,
			attemptId,
			event.structure,
			sanitizedName.Value(),
			authorized.Value().first,
			authorized.Value().second,
			m_ScratchDirectory);

		const JobId id = jobSystem->Submit(job, JobPriority::Normal);
		m_PendingJobs[id] = std::move(job);
	}

	void StructureLifecycleCoordinator::onRendererTabClosed(const DomainEvents::RendererTabClosed &event)
	{
		Ref<CreationSession> session = m_SessionRegistry->Find(event.sessionId).lock();
		if (session == nullptr)
		{
			// Expected traffic, not an error: the tab X, the NewStructure panel closing, and the
			// settings-driven auto-close after a successful add all publish this same event.
			DS_LOG_TRACE("Structure Lifecycle: close for already-removed session {}", ToString(event.sessionId));
			return;
		}

		session->state = CreationSessionState::Closing;

		// Removing the session detaches it from the UI immediately either way. If an attempt is still
		// in flight, its late completion lands in onJobCompleted with no session to find - which is
		// exactly the orphan path that deletes the unregistered directory.
		if (session->activeAttemptId.has_value())
		{
			DS_LOG_INFO("Structure Lifecycle: session {} closed with attempt {} still in flight; "
				"its result will be discarded and any written directory removed",
				ToString(event.sessionId), ToString(*session->activeAttemptId));
		}

		m_SessionRegistry->Remove(event.sessionId);
	}

	void StructureLifecycleCoordinator::discardOrphanedAttempt(const AddStructureToProjectJob &job)
	{
		const std::optional<Result<Path>> &outcome = job.GetOutcome();
		if (!outcome.has_value() || !outcome->HasValue())
			return; // Failure or no result: the job already cleaned up its own staging directory

		// Success for a session nobody is watching any more. The directory was never registered and
		// still carries its .pending_registration sentinel, so removing it loses nothing.
		const FilePath structureDirectory = outcome->Value().Native().parent_path();
		std::error_code error;
		FileSystem::RemoveAll(structureDirectory, error);
		if (error)
			DS_LOG_WARN("Structure Lifecycle: could not remove orphaned structure directory '{}': {}",
				structureDirectory.string(), error.message());
		else
			DS_LOG_INFO("Structure Lifecycle: removed orphaned structure directory '{}'", structureDirectory.string());
	}

	void StructureLifecycleCoordinator::onJobCompleted(const JobCompletedEvent &event)
	{
		auto it = m_PendingJobs.find(event.id);
		if (it == m_PendingJobs.end())
			return;

		Ref<AddStructureToProjectJob> job = std::move(it->second);
		m_PendingJobs.erase(it);

		const Uuid sessionId = job->GetSessionId();
		const Uuid attemptId = job->GetAttemptId();

		// Gate on session/attempt identity BEFORE touching the domain, so a stale result can never
		// register anything or publish a public event.
		Ref<CreationSession> session = m_SessionRegistry->Find(sessionId).lock();
		if (session == nullptr
			|| session->state == CreationSessionState::Closing
			|| session->activeAttemptId != attemptId)
		{
			discardOrphanedAttempt(*job);
			return;
		}

		const std::optional<Result<Path>> &outcome = job->GetOutcome();
		if (!outcome.has_value())
		{
			session->activeAttemptId.reset();
			session->state = CreationSessionState::Failed;
			StructuredError error = WorkflowError(
				"Adding the structure did not finish",
				"Job " + std::to_string(event.id) + " completed without an outcome",
				"Try adding the structure again.");
			session->lastError = error;
			publishFailure(sessionId, attemptId, std::move(error));
			return;
		}

		session->state = CreationSessionState::Completing;

		if (!outcome->HasValue())
		{
			session->activeAttemptId.reset();
			session->state = CreationSessionState::Failed;
			session->lastError = outcome->Error();
			publishFailure(sessionId, attemptId, outcome->Error());
			return;
		}

		const Path poscarPath = outcome->Value();

		Ref<DomainLayer> domainLayer = m_DomainLayer.lock();
		if (domainLayer == nullptr)
		{
			// The file is on disk but nothing can register it - remove it rather than leaving an
			// unreferenced directory behind.
			discardOrphanedAttempt(*job);
			session->activeAttemptId.reset();
			session->state = CreationSessionState::Failed;
			StructuredError error = WorkflowError(
				"Cannot add the structure right now",
				"Domain layer unavailable after writing " + poscarPath.String(),
				"Restart the application and try again.");
			session->lastError = error;
			publishFailure(sessionId, attemptId, std::move(error));
			return;
		}

		// The one canonical registration entry point for project structures.
		Result<StructureId> registered = domainLayer->Workspace().Structures().RegisterAsProjectMember(
			job->GetStructure(), poscarPath, job->GetDisplayName());

		if (!registered)
		{
			// Written but unregistered: drop the POSCAR so the directory cannot masquerade as a
			// structure directory. Its .pending_registration sentinel already marks whatever remains.
			std::error_code error;
			FileSystem::Remove(poscarPath.Native(), error);

			session->activeAttemptId.reset();
			session->state = CreationSessionState::Failed;
			session->lastError = registered.Error();
			publishFailure(sessionId, attemptId, registered.Error());
			return;
		}

		// Registration confirmed - clear the sentinel. This is the step a future startup scan's
		// contract depends on: a structure directory that still has one was never confirmed.
		std::error_code sentinelError;
		FileSystem::Remove(poscarPath.Native().parent_path() / PendingRegistrationSentinel, sentinelError);

		session->activeAttemptId.reset();
		session->state = CreationSessionState::Success;
		session->lastError.reset();
		session->lastModifiedAt = Time::Now();

		DomainEvents::ProjectStructureAdded successEvent;
		successEvent.sessionId = sessionId;
		successEvent.attemptId = attemptId;
		successEvent.newStructureId = registered.Value();
		successEvent.poscarPath = poscarPath.Native();
		m_EventBus->Publish(successEvent);

		DS_LOG_INFO("Add Structure: '{}' registered with id {}", job->GetDisplayName(), ToString(registered.Value()));
	}

	void StructureLifecycleCoordinator::onJobFailed(const JobFailedEvent &event)
	{
		auto it = m_PendingJobs.find(event.id);
		if (it == m_PendingJobs.end())
			return;

		Ref<AddStructureToProjectJob> job = std::move(it->second);
		m_PendingJobs.erase(it);

		// The job reports expected failures through its outcome and completes normally, so reaching
		// here means it threw: an unexpected bug or a cancellation, not a validation result.
		const Uuid sessionId = job->GetSessionId();
		const Uuid attemptId = job->GetAttemptId();

		Ref<CreationSession> session = m_SessionRegistry->Find(sessionId).lock();
		if (session == nullptr || session->activeAttemptId != attemptId)
		{
			DS_LOG_WARN("Add Structure: job {} failed for a session that is gone: {}", event.id, event.errorMessage);
			return;
		}

		StructuredError error = WorkflowError(
			"Failed to add structure to project", event.errorMessage, "See the log for details, then retry.");
		session->activeAttemptId.reset();
		session->state = CreationSessionState::Failed;
		session->lastError = error;
		publishFailure(sessionId, attemptId, std::move(error));

		DS_LOG_WARN("Add Structure: job {} failed: {}", event.id, event.errorMessage);
	}
} // namespace DefectStudio
