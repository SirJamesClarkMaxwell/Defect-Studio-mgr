#pragma once

#include <unordered_map>
#include <vector>

#include "App/CreationSession.hpp"
#include "App/Jobs/AddStructureToProjectJob.hpp"
#include "Core/Domain/StructureLifecycleEvents.hpp"
#include "Core/EventSystem/BusEventSystem/EventReceiver.hpp"
#include "Core/JobSystem/JobEvents.hpp"
#include "Core/JobSystem/JobSystem.hpp"
#include "Core/Utils/Memory.hpp"
#include "Events/ProjectEvents.hpp"

namespace DefectStudio
{
	class EventBus;
	class DomainLayer;

	// Owns the add-to-project workflow end to end: session state, attempt identity, target
	// authorization, job submission, domain registration, and the public success/failure events.
	//
	// Lives in App because it is the only place that can see all four of JobSystem, DomainLayer,
	// EventBus and the project roots. Domain stays event-free; the UI only collects intent.
	//
	// Two rules shape everything here:
	//  - A session has at most one attempt in flight. This is enforced here, on the main thread, by
	//    a plain check-and-set - not by the UI disabling a button, which is only defence in depth.
	//    With concurrent retries impossible by construction, "stale completion" collapses to the one
	//    case below.
	//  - Domain registration happens BEFORE ProjectStructureAdded is published, so nothing can
	//    observe a success event for a structure the domain does not have.
	//
	// The one stale case: the tab was closed while its attempt was still running. The session is gone
	// from the registry by then, so a late success is a directory nobody asked for any more - it was
	// never registered, nothing references it, and it still carries its .pending_registration
	// sentinel, so it is deleted rather than left to rot.
	class StructureLifecycleCoordinator final : public EventReceiver
	{
	public:
		StructureLifecycleCoordinator(
			Ref<EventBus> eventBus,
			WeakRef<JobSystem> jobSystem,
			WeakRef<DomainLayer> domainLayer,
			Ref<CreationSessionRegistry> sessionRegistry,
			Path scratchDirectory);
		~StructureLifecycleCoordinator();

	private:
		void onProjectRootsChanged(const ProjectEvents::ProjectRootsChanged &event);
		void onSessionReadyForStructureHub(const DomainEvents::SessionReadyForStructureHub &event);
		void onAddStructureToProjectRequested(const DomainEvents::AddStructureToProjectRequested &event);
		void onRendererTabClosed(const DomainEvents::RendererTabClosed &event);
		void onJobCompleted(const JobCompletedEvent &event);
		void onJobFailed(const JobFailedEvent &event);

		// Resolves `targetDirectory` to a canonical path plus the project root that authorizes it.
		[[nodiscard]] Result<std::pair<Path, Path>> authorizeTarget(const FilePath &targetDirectory) const;
		void publishFailure(const Uuid &sessionId, const Uuid &attemptId, StructuredError error);
		// Late completion of an attempt whose session is gone: undo whatever reached disk.
		void discardOrphanedAttempt(const AddStructureToProjectJob &job);

		Ref<EventBus> m_EventBus;
		WeakRef<JobSystem> m_JobSystem;
		WeakRef<DomainLayer> m_DomainLayer;
		Ref<CreationSessionRegistry> m_SessionRegistry;
		// Scratch space for the Python bridge's per-attempt input file. Deliberately outside any
		// project directory, so a cleanup failure there can never leak into a committed structure.
		Path m_ScratchDirectory;
		// Canonicalized once here, on receipt: every later check compares against these, so
		// "reject symlinks" and "roots are canonical" are one policy applied at different times.
		std::vector<Path> m_ProjectRoots;
		std::unordered_map<JobId, Ref<AddStructureToProjectJob>> m_PendingJobs;
	};
} // namespace DefectStudio
