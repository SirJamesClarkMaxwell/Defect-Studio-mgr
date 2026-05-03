#pragma once

#include "Core/EventSystem/BusEventSystem/EventReceiver.hpp"
#include "Core/Layer.hpp"
#include "Core/Utils/Memory.hpp"

namespace DefectStudio
{
	class EventBus;

	namespace AppEvents::Config
	{
		struct PersistRequested;
	}

	namespace EditorUiEvents
	{
		struct PersistRequested;
		struct LayoutLoadRequested;
		struct LayoutLoaded;
		struct LayoutLoadFailed;
		struct ThemeSaveRequested;
		struct ThemeLoadRequested;
		struct ThemeLoaded;
		struct ThemeLoadFailed;
	}

	class IOLayer final : public Layer, public EventReceiver
	{
	public:
		IOLayer();

		void BindRuntimeServices(Ref<EventBus> eventBus);

		void OnAttach() override;
		void OnDetach() override;
		void OnUpdate(float deltaTime) override;

	private:
		void bindConfigPersistenceEvents();
		void onConfigPersistRequested(const AppEvents::Config::PersistRequested &event);
		void onUiPersistRequested(const EditorUiEvents::PersistRequested &event);
		void onThemeSaveRequested(const EditorUiEvents::ThemeSaveRequested &event);
		void onThemeLoadRequested(const EditorUiEvents::ThemeLoadRequested &event);
		void onLayoutLoadRequested(const EditorUiEvents::LayoutLoadRequested &event);

	private:
		Ref<EventBus> m_EventBus;
	};
} // namespace DefectStudio
