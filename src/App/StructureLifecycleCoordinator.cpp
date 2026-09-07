#include "Core/dspch.hpp"

#include "App/StructureLifecycleCoordinator.hpp"

#include <functional>
#include <utility>

#include "Core/Domain/StructureLifecycleEvents.hpp"
#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Core/Logging/Logger.hpp"
#include "Domain/DomainLayer.hpp"

namespace DefectStudio
{
	StructureLifecycleCoordinator::StructureLifecycleCoordinator(
		Ref<EventBus> eventBus,
		WeakRef<JobSystem> jobSystem,
		WeakRef<DomainLayer> domainLayer)
		: m_EventBus(std::move(eventBus)),
		  m_JobSystem(std::move(jobSystem)),
		  m_DomainLayer(std::move(domainLayer))
	{
		if (m_EventBus == nullptr)
			return;

		AddSubscription(m_EventBus->Subscribe<DomainEvents::AddStructureToProjectRequested>(
			std::bind_front(&StructureLifecycleCoordinator::onAddStructureToProjectRequested, this)));
		AddSubscription(m_EventBus->Subscribe<JobCompletedEvent>(
			std::bind_front(&StructureLifecycleCoordinator::onJobCompleted, this)));
		AddSubscription(m_EventBus->Subscribe<JobFailedEvent>(
			std::bind_front(&StructureLifecycleCoordinator::onJobFailed, this)));
	}

	StructureLifecycleCoordinator::~StructureLifecycleCoordinator()
	{
		ClearSubscriptions();
	}

	void StructureLifecycleCoordinator::onAddStructureToProjectRequested(
		const DomainEvents::AddStructureToProjectRequested &event)
	{
		Ref<JobSystem> jobSystem = m_JobSystem.lock();
		if (jobSystem == nullptr)
		{
			DS_LOG_ERROR("Add Structure: JobSystem unavailable, cannot add '{}'", event.displayName);
			return;
		}

		Ref<AddStructureToProjectJob> job = CreateRef<AddStructureToProjectJob>(
			event.structure, event.displayName, event.targetDirectory);
		const JobId id = jobSystem->Submit(job, JobPriority::Normal);
		m_PendingJobs[id] = std::move(job);
	}

	void StructureLifecycleCoordinator::onJobCompleted(const JobCompletedEvent &event)
	{
		auto it = m_PendingJobs.find(event.id);
		if (it == m_PendingJobs.end())
			return;

		Ref<AddStructureToProjectJob> job = std::move(it->second);
		m_PendingJobs.erase(it);

		const std::optional<Path> &poscarPath = job->GetResult();
		if (!poscarPath.has_value())
		{
			DS_LOG_ERROR("Add Structure: job {} completed with no result", event.id);
			return;
		}

		Ref<DomainLayer> domainLayer = m_DomainLayer.lock();
		if (domainLayer == nullptr)
		{
			DS_LOG_ERROR("Add Structure: Domain layer unavailable, dropping added structure");
			return;
		}

		// Register structure in domain (sourcePath set to POSCAR path, revision=0/savedRevision=0)
		// This is the ONLY place where RegisterAsProjectMember gets called - the canonical entry point.
		const Ref<const StructureRecord> record = domainLayer->Workspace().Structures().RegisterAsProjectMember(
			job->GetStructure(),
			*poscarPath,
			job->GetDisplayName());

		// Publish success event
		DomainEvents::ProjectStructureAdded successEvent;
		successEvent.newStructureId = record->id;
		successEvent.poscarPath = *poscarPath;
		m_EventBus->Publish(successEvent);

		DS_LOG_INFO("Add Structure: structure '{}' registered with id {}", record->displayName, ToString(record->id));
	}

	void StructureLifecycleCoordinator::onJobFailed(const JobFailedEvent &event)
	{
		auto it = m_PendingJobs.find(event.id);
		if (it == m_PendingJobs.end())
			return;

		m_PendingJobs.erase(it);

		// Publish failure event
		DomainEvents::ProjectStructureAddFailed failureEvent;
		failureEvent.error.technicalDetails = event.errorMessage;
		failureEvent.error.userMessage = "Failed to add structure to project";
		m_EventBus->Publish(failureEvent);

		DS_LOG_WARN("Add Structure: job {} failed: {}", event.id, event.errorMessage);
	}
} // namespace DefectStudio
