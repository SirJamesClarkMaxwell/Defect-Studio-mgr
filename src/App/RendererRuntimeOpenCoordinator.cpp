#include "Core/dspch.hpp"

#include "App/RendererRuntimeOpenCoordinator.hpp"

#include <functional>
#include <utility>

#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Core/Logging/Logger.hpp"
#include "Core/Utils/Uuid.hpp"
#include "Domain/Crystal/BondGenerator.hpp"
#include "Domain/DomainLayer.hpp"
#include "Renderer/RendererLayer.hpp"
#include "Renderer/RendererStartupBootstrap.hpp"
#include "Renderer/StructureRendererDataBuilder.hpp"
#include "ScientificRuntime/Python/OpenDefectJob.hpp"
#include "ScientificRuntime/Python/PymatgenConversion.hpp"

namespace DefectStudio
{
	RendererRuntimeOpenCoordinator::RendererRuntimeOpenCoordinator(
		Ref<EventBus> eventBus,
		WeakRef<JobSystem> jobSystem,
		WeakRef<DomainLayer> domainLayer,
		WeakRef<RendererLayer> rendererLayer,
		AtomStyleTable atomStyleTable,
		ElementPropertiesTable elementPropertiesTable)
		: m_EventBus(std::move(eventBus)),
		  m_JobSystem(std::move(jobSystem)),
		  m_DomainLayer(std::move(domainLayer)),
		  m_RendererLayer(std::move(rendererLayer)),
		  m_AtomStyleTable(std::move(atomStyleTable)),
		  m_ElementPropertiesTable(std::move(elementPropertiesTable))
	{
		if (m_EventBus == nullptr)
			return;

		AddSubscription(m_EventBus->Subscribe<RendererEvents::Windows::OpenStructureRequested>(
			std::bind_front(&RendererRuntimeOpenCoordinator::onOpenStructureRequested, this)));
		AddSubscription(m_EventBus->Subscribe<JobCompletedEvent>(
			std::bind_front(&RendererRuntimeOpenCoordinator::onJobCompleted, this)));
		AddSubscription(m_EventBus->Subscribe<JobFailedEvent>(
			std::bind_front(&RendererRuntimeOpenCoordinator::onJobFailed, this)));
	}

	RendererRuntimeOpenCoordinator::~RendererRuntimeOpenCoordinator()
	{
		ClearSubscriptions();
	}

	void RendererRuntimeOpenCoordinator::onOpenStructureRequested(
		const RendererEvents::Windows::OpenStructureRequested &event)
	{
		Ref<JobSystem> jobSystem = m_JobSystem.lock();
		if (jobSystem == nullptr)
		{
			DS_LOG_ERROR("Open Defect: JobSystem unavailable, cannot open '{}'", event.filePath.String());
			return;
		}

		Ref<OpenDefectJob> job = CreateRef<OpenDefectJob>(event.filePath, event.displayName);
		const JobId id = jobSystem->Submit(job, JobPriority::Normal);
		m_PendingJobs[id] = std::move(job);
	}

	void RendererRuntimeOpenCoordinator::onJobCompleted(const JobCompletedEvent &event)
	{
		auto it = m_PendingJobs.find(event.id);
		if (it == m_PendingJobs.end())
			return;

		Ref<OpenDefectJob> job = std::move(it->second);
		m_PendingJobs.erase(it);

		const std::optional<PymatgenStructureData> &loaded = job->GetResult();
		if (!loaded.has_value())
		{
			DS_LOG_ERROR("Open Defect: job {} completed with no result", event.id);
			return;
		}

		Ref<DomainLayer> domainLayer = m_DomainLayer.lock();
		Ref<RendererLayer> rendererLayer = m_RendererLayer.lock();
		if (domainLayer == nullptr || rendererLayer == nullptr)
		{
			DS_LOG_ERROR("Open Defect: Domain/Renderer layer unavailable, dropping loaded structure");
			return;
		}

		// Same three-step sequence RendererStartupComposer.cpp runs once at startup, made callable
		// at runtime: RegenerateAutoBonds -> Workspace().Structures().Add -> BuildRendererStructureData.
		CrystalStructure domainStructureValue = ConvertPymatgenStructureToCrystalStructure(*loaded, job->GetDisplayName());
		RegenerateAutoBonds(domainStructureValue, m_ElementPropertiesTable);

		Ref<const StructureRecord> structureRecord = domainLayer->Workspace().Structures().Add(
			std::move(domainStructureValue), job->GetFilePath(), job->GetDisplayName());

		RendererStartupWindowInput input;
		input.definition.title = job->GetDisplayName();
		input.definition.structureName = job->GetDisplayName();
		input.definition.poscarPath = job->GetFilePath();
		input.structure = BuildRendererStructureData(
			structureRecord->structure,
			job->GetFilePath(),
			job->GetDisplayName(),
			m_AtomStyleTable,
			ToString(structureRecord->id));

		std::vector<RendererStartupWindowInput> inputs;
		inputs.push_back(std::move(input));
		std::vector<RendererWindowState> windows = BuildRendererStartupWindows(std::move(inputs));
		if (!windows.empty())
			rendererLayer->AddWindow(std::move(windows.front()));
		else
			DS_LOG_ERROR("Open Defect: failed to build renderer window for '{}'", job->GetDisplayName());
	}

	void RendererRuntimeOpenCoordinator::onJobFailed(const JobFailedEvent &event)
	{
		auto it = m_PendingJobs.find(event.id);
		if (it == m_PendingJobs.end())
			return;

		DS_LOG_ERROR("Open Defect failed: {}", event.errorMessage);
		m_PendingJobs.erase(it);
	}
} // namespace DefectStudio
