#pragma once

#include <unordered_map>

#include "Core/EventSystem/BusEventSystem/EventReceiver.hpp"
#include "Core/JobSystem/JobEvents.hpp"
#include "Core/JobSystem/JobSystem.hpp"
#include "Core/Utils/Memory.hpp"
#include "Domain/Crystal/ElementProperties.hpp"
#include "Events/RendererEvents.hpp"
#include "Renderer/AtomStyleTable.hpp"

namespace DefectStudio
{
	class EventBus;
	class DomainLayer;
	class RendererLayer;
	class OpenDefectJob;

	// Bridges RendererEvents::Windows::OpenStructureRequested -> JobSystem -> DomainLayer ->
	// RendererLayer::AddWindow at runtime. Mirrors what RendererStartupComposer does once at startup,
	// but event-driven, after the app is already running.
	//
	// Lives in App, not Renderer: this is exactly the App-as-composition-root boundary from
	// docs/work/project/TODO.md ("App skalda systemy... Renderer nie jest źródłem prawdy domenowej") -
	// RendererLayer stays domain-agnostic (AddWindow just takes an already-built RendererWindowState),
	// and DomainLayer stays renderer-agnostic. Only this App-layer object knows about both.
	class RendererRuntimeOpenCoordinator final : public EventReceiver
	{
	public:
		RendererRuntimeOpenCoordinator(
			Ref<EventBus> eventBus,
			WeakRef<JobSystem> jobSystem,
			WeakRef<DomainLayer> domainLayer,
			WeakRef<RendererLayer> rendererLayer,
			AtomStyleTable atomStyleTable,
			ElementPropertiesTable elementPropertiesTable);
		~RendererRuntimeOpenCoordinator();

	private:
		void onOpenStructureRequested(const RendererEvents::Windows::OpenStructureRequested &event);
		void onJobCompleted(const JobCompletedEvent &event);
		void onJobFailed(const JobFailedEvent &event);

		Ref<EventBus> m_EventBus;
		WeakRef<JobSystem> m_JobSystem;
		WeakRef<DomainLayer> m_DomainLayer;
		WeakRef<RendererLayer> m_RendererLayer;
		AtomStyleTable m_AtomStyleTable;
		ElementPropertiesTable m_ElementPropertiesTable;
		std::unordered_map<JobId, Ref<OpenDefectJob>> m_PendingJobs;
	};
} // namespace DefectStudio
