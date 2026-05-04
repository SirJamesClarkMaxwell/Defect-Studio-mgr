#pragma once

#include "Core/Layer.hpp"
#include "Core/Utils/Memory.hpp"

namespace DefectStudio
{
	class AssetManager;
	class EventBus;
	class Notifier;
}

namespace DefectStudio::Demo
{
	class DemoBackendRuntime;
	class DemoCapabilitiesPanel;
	class DemoNotificationsPanel;
	class EventDispatcherDemo;
	class EventBusDemo;
	class JobSystemDemo;

	class DemoLayer final : public Layer
	{
	public:
		explicit DemoLayer(WeakRef<AssetManager> assetManager = {});
		~DemoLayer();

		void OnAttach() override;
		void OnDetach() override;
		void OnEvent(Event &event) override;
		void OnImGuiRender() override;

	private:
		WeakRef<AssetManager> m_AssetManager;
		Ref<EventBus> m_DemoEventBus;
		Ref<Notifier> m_DemoNotifier;
		Unique<EventDispatcherDemo> m_EventDispatcherDemo;
		Unique<EventBusDemo> m_EventBusDemo;
		Unique<JobSystemDemo> m_JobSystemDemo;
		Unique<DemoNotificationsPanel> m_NotificationsPanel;
		Unique<DemoCapabilitiesPanel> m_CapabilitiesPanel;
		Unique<DemoBackendRuntime> m_BackendRuntime;
	};
} // namespace DefectStudio::Demo
