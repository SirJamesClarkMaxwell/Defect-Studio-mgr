#pragma once

#include <unordered_map>

#include "Core/EventSystem/BusEventSystem/EventReceiver.hpp"
#include "Core/JobSystem/JobEvents.hpp"
#include "Core/JobSystem/JobSystem.hpp"
#include "Core/Utils/Memory.hpp"
#include "App/Jobs/AddStructureToProjectJob.hpp"

namespace DefectStudio
{
	class EventBus;
	class DomainLayer;

	// Orchestrates structure lifecycle: listens to AddStructureToProjectRequested (UI),
	// submits AddStructureToProjectJob via JobSystem, registers completed structures in domain,
	// and publishes ProjectStructureAdded or ProjectStructureAddFailed events.
	//
	// Lives in App, not Domain/UI: composition root for cross-layer concerns (JobSystem + Domain + EventBus).
	// Domain stays pure (no event coupling); UI collects intent only.
	class StructureLifecycleCoordinator final : public EventReceiver
	{
	public:
		StructureLifecycleCoordinator(
			Ref<EventBus> eventBus,
			WeakRef<JobSystem> jobSystem,
			WeakRef<DomainLayer> domainLayer);
		~StructureLifecycleCoordinator();

	private:
		void onAddStructureToProjectRequested(const DomainEvents::AddStructureToProjectRequested &event);
		void onJobCompleted(const JobCompletedEvent &event);
		void onJobFailed(const JobFailedEvent &event);

		Ref<EventBus> m_EventBus;
		WeakRef<JobSystem> m_JobSystem;
		WeakRef<DomainLayer> m_DomainLayer;
		std::unordered_map<JobId, Ref<AddStructureToProjectJob>> m_PendingJobs;
	};
} // namespace DefectStudio
