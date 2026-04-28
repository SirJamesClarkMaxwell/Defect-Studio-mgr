#include "Core/dspch.hpp"

#include "IO/IOLayer.hpp"

#include <functional>

#include "App/Events/ApplicationConfigEvents.hpp"
#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Core/Utils/Logger.hpp"
#include "Core/Utils/Path.hpp"
#include "IO/TextFileIO.hpp"
#include "Presentation/EditorUiEvents.hpp"

namespace DefectStudio
{
	namespace
	{
		template <typename EventType>
		SubscriptionHandle subscribeIOLayer(
			EventBus &bus,
			IOLayer &layer,
			void (IOLayer::*method)(const EventType &))
		{
			return bus.Subscribe<EventType>(std::bind_front(method, &layer));
		}

	} // namespace

	IOLayer::IOLayer() : Layer("IOLayer")
	{
	}

	void IOLayer::BindRuntimeServices(Ref<EventBus> eventBus)
	{
		ClearSubscriptions();
		m_EventBus = std::move(eventBus);
		bindConfigPersistenceEvents();
	}

	void IOLayer::OnAttach()
	{
		DS_LOG_INFO("IOLayer attached");
	}

	void IOLayer::OnDetach()
	{
		ClearSubscriptions();
		m_EventBus.reset();
		DS_LOG_INFO("IOLayer detached");
	}

	void IOLayer::OnUpdate(float deltaTime)
	{
		(void)deltaTime;
	}

	void IOLayer::bindConfigPersistenceEvents()
	{
		if (m_EventBus == nullptr)
		{
			DS_LOG_WARN("IOLayer config persistence events not bound: EventBus unavailable");
			return;
		}

		using namespace AppEvents::Config;

		AddSubscription(subscribeIOLayer<PersistRequested>(*m_EventBus, *this, &IOLayer::onConfigPersistRequested));
		AddSubscription(subscribeIOLayer<EditorUiEvents::PersistRequested>(*m_EventBus, *this, &IOLayer::onUiPersistRequested));
		AddSubscription(subscribeIOLayer<EditorUiEvents::ThemeLoadRequested>(*m_EventBus, *this, &IOLayer::onThemeLoadRequested));
		AddSubscription(subscribeIOLayer<EditorUiEvents::LayoutLoadRequested>(*m_EventBus, *this, &IOLayer::onLayoutLoadRequested));
		DS_LOG_INFO("IOLayer config persistence event handlers bound");
	}

	void IOLayer::onConfigPersistRequested(const AppEvents::Config::PersistRequested &event)
	{
		using namespace AppEvents::Config;

		if (m_EventBus == nullptr)
			return;

		DS_LOG_INFO("Persisting user config: path={}", event.path.String());
		std::string error;
		if (!TextFileIO::Save(event.path, event.contents, error))
		{
			DS_LOG_ERROR("User config persistence failed: {}", error);
			if (event.kind == PersistKind::UserSettings)
				m_EventBus->Queue(UserSaveFailed{event.config, error});
			else
				m_EventBus->Queue(DefaultsSaveFailed{event.config, error});
			return;
		}

		if (event.kind == PersistKind::UserSettings)
			m_EventBus->Queue(UserSaved{event.config});
		else
			m_EventBus->Queue(DefaultsSaved{event.config});
		DS_LOG_INFO("User config persisted");
	}

	void IOLayer::onUiPersistRequested(const EditorUiEvents::PersistRequested &event)
	{
		using namespace EditorUiEvents;

		if (m_EventBus == nullptr)
			return;

		DS_LOG_INFO("Persisting UI file: path={}", event.path.String());
		std::string error;
		if (!TextFileIO::Save(event.path, event.contents, error))
		{
			DS_LOG_ERROR("UI file persistence failed: {}", error);
			if (event.kind == PersistKind::Theme)
				m_EventBus->Queue(ThemeSaveFailed{event.path, error});
			else
				m_EventBus->Queue(LayoutSaveFailed{event.path, error});
			return;
		}

		if (event.kind == PersistKind::Theme)
			m_EventBus->Queue(ThemeSaved{event.path});
		else
			m_EventBus->Queue(LayoutSaved{event.path, event.contents.size()});
		DS_LOG_INFO("UI file persisted: {}", event.path.String());
	}

	void IOLayer::onThemeLoadRequested(const EditorUiEvents::ThemeLoadRequested &event)
	{
		using namespace EditorUiEvents;

		if (m_EventBus == nullptr)
			return;

		std::string text;
		std::string error;
		if (!TextFileIO::Load(event.path, text, error))
		{
			DS_LOG_WARN("Appearance theme load failed [{}]: {}", event.path.String(), error);
			m_EventBus->Queue(ThemeLoadFailed{event.path, error});
			return;
		}

		m_EventBus->Queue(ThemeLoaded{event.path, std::move(text)});
	}

	void IOLayer::onLayoutLoadRequested(const EditorUiEvents::LayoutLoadRequested &event)
	{
		using namespace EditorUiEvents;

		if (m_EventBus == nullptr)
			return;

		std::string text;
		std::string error;
		if (!TextFileIO::Load(event.path, text, error))
		{
			DS_LOG_WARN("Layout load failed [{}]: {}", event.path.String(), error);
			m_EventBus->Queue(LayoutLoadFailed{event.path, error});
			return;
		}

		m_EventBus->Queue(LayoutLoaded{event.path, std::move(text)});
	}
} // namespace DefectStudio
