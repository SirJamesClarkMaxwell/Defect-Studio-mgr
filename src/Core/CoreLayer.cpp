#include "Core/dspch.hpp"


#include <algorithm>
#include <functional>

#include "Core/Utils/Logger.hpp"
#include "Core/Utils/Assert.hpp"
#include "Core/CoreLayer.hpp"
#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Core/EventSystem/DispatchingEventSystem/PlatformEvents/KeyboardEvents.hpp"
#include "Core/EventSystem/DispatchingEventSystem/PlatformEvents/PlatformEventBase.hpp"
#include "Core/Input/KeyInputProcessor.hpp"
#include "Core/JobSystem/JobSystem.hpp"
#include "Core/JobSystem/JobSystemConfigEvents.hpp"
#include "Core/JobSystem/JobEvents.hpp"
#include "Core/ProgressTrackingSystem/ProgressTracker.hpp"
#include "Core/Commands/SystemCommands.hpp"
#include "Core/Utils/Input.hpp"

namespace DefectStudio
{
	namespace
	{
		template <typename EventType>
		SubscriptionHandle subscribeCoreLayer(
			EventBus &bus,
			CoreLayer &layer,
			void (CoreLayer::*method)(const EventType &))
		{
			return bus.Subscribe<EventType>(std::bind_front(method, &layer));
		}

		std::size_t resolveWorkerThreadCount(const JobsConfig &jobsConfig)
		{
			const std::size_t urgentWorkerCount = static_cast<std::size_t>(jobsConfig.reserveUrgentWorker ? 1 : 0);
			return static_cast<std::size_t>(std::max(1, jobsConfig.defaultWorkerThreadCount)) + urgentWorkerCount;
		}

		[[nodiscard]] KeyModifiers currentKeyModifiers()
		{
			KeyModifiers modifiers = KeyModifiers::None;
			if (Input::IsKeyDown(KeyCode::LeftControl) || Input::IsKeyDown(KeyCode::RightControl))
				modifiers = modifiers | KeyModifiers::Ctrl;
			if (Input::IsKeyDown(KeyCode::LeftShift) || Input::IsKeyDown(KeyCode::RightShift))
				modifiers = modifiers | KeyModifiers::Shift;
			if (Input::IsKeyDown(KeyCode::LeftAlt) || Input::IsKeyDown(KeyCode::RightAlt))
				modifiers = modifiers | KeyModifiers::Alt;
			if (Input::IsKeyDown(KeyCode::LeftSuper) || Input::IsKeyDown(KeyCode::RightSuper))
				modifiers = modifiers | KeyModifiers::Super;
			return modifiers;
		}
	}

	CoreLayer::CoreLayer() : Layer("CoreLayer")
	{
	}

	void CoreLayer::OnAttach()
	{
		DS_LOG_INFO("CoreLayer attached");
	}

	void CoreLayer::OnDetach()
	{
		ShutdownSystems();
		DS_LOG_INFO("CoreLayer detached");
	}

	void CoreLayer::OnUpdate(float deltaTime)
	{
		m_AccumulatedTime += deltaTime;
		if (m_EventBus)
			m_EventBus->ProcessQueue();
	}

	void CoreLayer::OnEvent(Event &event)
	{
		if (!m_SystemsInitialized || event.handled)
			return;

		EventDispatcher dispatcher(event);
		dispatcher.Dispatch<KeyPressedEvent>(std::bind_front(&CoreLayer::onKeyPressed, this));
	}

	bool CoreLayer::InitializeSystems(Ref<EventBus> eventBus, const JobsConfig &jobsConfig)
	{
		if (m_SystemsInitialized)
		{
			DS_LOG_WARN("CoreLayer::InitializeSystems called more than once; call ignored");
			return true;
		}

		m_EventBus = std::move(eventBus);
		if (!m_EventBus)
		{
			DS_LOG_ERROR("CoreLayer::InitializeSystems requires a valid EventBus");
			return false;
		}

		const std::size_t workerThreadCount = resolveWorkerThreadCount(jobsConfig);
		DS_LOG_INFO(
			"Init: JobSystem worker_count={} (base={} reserve_urgent={})",
			workerThreadCount,
			jobsConfig.defaultWorkerThreadCount,
			jobsConfig.reserveUrgentWorker);
		m_JobSystem = CreateRef<JobSystem>(m_EventBus, workerThreadCount);

		DS_LOG_INFO("Init: ProgressTracker");
		m_ProgressTracker = CreateRef<ProgressTracker>(m_EventBus);

		m_UndoStack = CreateRef<UndoStack>();
		m_CommandRegistry = CreateRef<CommandRegistry>();
		m_CommandRegistry->SetUndoStack(CreateWeakRef(m_UndoStack));
		m_CommandService = CreateRef<CommandService>(m_CommandRegistry, CreateWeakRef(m_UndoStack));
		m_KeymapResolver = CreateRef<KeymapResolver>();
		m_ContextManager = CreateRef<ContextManager>();
		registerSystemCommands();

		AddSubscription(subscribeCoreLayer<JobSystemConfigAppliedEvent>(
			*m_EventBus,
			*this,
			&CoreLayer::onJobSystemConfigApplied));
		AddSubscription(subscribeCoreLayer<JobSubmitRequested>(*m_EventBus, *this, &CoreLayer::onJobSubmitRequested));
		AddSubscription(subscribeCoreLayer<JobCancelRequested>(*m_EventBus, *this, &CoreLayer::onJobCancelRequested));
		AddSubscription(subscribeCoreLayer<JobResumeRequested>(*m_EventBus, *this, &CoreLayer::onJobResumeRequested));
		AddSubscription(subscribeCoreLayer<JobResetRequested>(*m_EventBus, *this, &CoreLayer::onJobResetRequested));
		AddSubscription(subscribeCoreLayer<JobRetryRequested>(*m_EventBus, *this, &CoreLayer::onJobRetryRequested));
		AddSubscription(subscribeCoreLayer<JobHistoryRemoveRequested>(*m_EventBus, *this, &CoreLayer::onJobHistoryRemoveRequested));
		AddSubscription(subscribeCoreLayer<AppEvents::Keymap::BindingsLoaded>(*m_EventBus, *this, &CoreLayer::onBindingsLoaded));

		m_SystemsInitialized = true;
		DS_LOG_INFO(
			"Init complete: runtime services worker_count={} (base={} reserve_urgent={})",
			m_JobSystem->GetThreadCount(),
			jobsConfig.defaultWorkerThreadCount,
			jobsConfig.reserveUrgentWorker);
		return true;
	}

	void CoreLayer::ShutdownSystems()
	{
		if (!m_SystemsInitialized)
			return;

		DS_LOG_INFO("Shutdown: ProgressTracker");
		m_ProgressTracker.reset();
		m_ContextManager.reset();
		m_KeymapResolver.reset();
		m_CommandService.reset();
		m_CommandRegistry.reset();
		m_UndoStack.reset();
		DS_LOG_INFO("Shutdown: JobSystem");
		m_JobSystem.reset();
		ClearSubscriptions();
		m_EventBus.reset();
		m_SystemsInitialized = false;
	}

	EventBus &CoreLayer::GetEventBus()
	{
		DS_ASSERT(m_EventBus != nullptr, "EventBus is not initialized");
		return *m_EventBus;
	}

	JobSystem &CoreLayer::GetJobSystem()
	{
		DS_ASSERT(m_JobSystem != nullptr, "JobSystem is not initialized");
		return *m_JobSystem;
	}

	ProgressTracker &CoreLayer::GetProgressTracker()
	{
		DS_ASSERT(m_ProgressTracker != nullptr, "ProgressTracker is not initialized");
		return *m_ProgressTracker;
	}

	CommandService &CoreLayer::GetCommandService()
	{
		DS_ASSERT(m_CommandService != nullptr, "CommandService is not initialized");
		return *m_CommandService;
	}

	KeymapResolver &CoreLayer::GetKeymapResolver()
	{
		DS_ASSERT(m_KeymapResolver != nullptr, "KeymapResolver is not initialized");
		return *m_KeymapResolver;
	}

	ContextManager &CoreLayer::GetContextManager()
	{
		DS_ASSERT(m_ContextManager != nullptr, "ContextManager is not initialized");
		return *m_ContextManager;
	}

	UndoStack &CoreLayer::GetUndoStack()
	{
		DS_ASSERT(m_UndoStack != nullptr, "UndoStack is not initialized");
		return *m_UndoStack;
	}

	WeakRef<JobSystem> CoreLayer::GetJobSystemHandle() const
	{
		if (m_JobSystem == nullptr)
			return {};
		return CreateWeakRef(m_JobSystem);
	}

	WeakRef<ProgressTracker> CoreLayer::GetProgressTrackerHandle() const
	{
		if (m_ProgressTracker == nullptr)
			return {};
		return CreateWeakRef(m_ProgressTracker);
	}

	WeakRef<CommandService> CoreLayer::GetCommandServiceHandle() const
	{
		if (m_CommandService == nullptr)
			return {};
		return CreateWeakRef(m_CommandService);
	}

	WeakRef<KeymapResolver> CoreLayer::GetKeymapResolverHandle() const
	{
		if (m_KeymapResolver == nullptr)
			return {};
		return CreateWeakRef(m_KeymapResolver);
	}

	WeakRef<ContextManager> CoreLayer::GetContextManagerHandle() const
	{
		if (m_ContextManager == nullptr)
			return {};
		return CreateWeakRef(m_ContextManager);
	}

	WeakRef<CommandRegistry> CoreLayer::GetCommandRegistryHandle() const
	{
		if (m_CommandRegistry == nullptr)
			return {};
		return CreateWeakRef(m_CommandRegistry);
	}

	void CoreLayer::applyJobConfig(const JobsConfig &jobsConfig)
	{
		if (m_JobSystem == nullptr)
		{
			DS_LOG_WARN("CoreLayer: job config apply skipped because JobSystem is unavailable");
			return;
		}

		const std::size_t targetThreadCount = resolveWorkerThreadCount(jobsConfig);
		const std::size_t previousThreadCount = m_JobSystem->GetThreadCount();
		if (!m_JobSystem->SetThreadCount(targetThreadCount))
		{
			DS_LOG_WARN(
				"CoreLayer: failed to apply worker count target={} previous={}",
				targetThreadCount,
				previousThreadCount);
			return;
		}

		DS_LOG_INFO(
			"CoreLayer: worker count applied previous={} target={} (base={} reserve_urgent={})",
			previousThreadCount,
			targetThreadCount,
			jobsConfig.defaultWorkerThreadCount,
			jobsConfig.reserveUrgentWorker);
	}

	void CoreLayer::onJobSystemConfigApplied(const JobSystemConfigAppliedEvent &event)
	{
		DS_LOG_INFO("CoreLayer received job system config applied event");
		applyJobConfig(event.jobs);
	}

	void CoreLayer::onJobSubmitRequested(const JobSubmitRequested &event)
	{
		if (m_JobSystem == nullptr || event.job == nullptr)
			return;

		const JobId id = m_JobSystem->Submit(event.job, event.priority);
		DS_LOG_INFO("CoreLayer submitted requested job id={} source={}", id, event.source.empty() ? "<unknown>" : event.source);
	}

	void CoreLayer::onJobCancelRequested(const JobCancelRequested &event)
	{
		if (m_JobSystem == nullptr)
			return;

		for (const JobId id : event.ids)
			(void)m_JobSystem->RequestCancel(id);
	}

	void CoreLayer::onJobResumeRequested(const JobResumeRequested &event)
	{
		if (m_JobSystem == nullptr)
			return;

		for (const JobId id : event.ids)
			(void)m_JobSystem->Resume(id);
	}

	void CoreLayer::onJobResetRequested(const JobResetRequested &event)
	{
		if (m_JobSystem == nullptr || event.id == 0)
			return;

		(void)m_JobSystem->Reset(event.id);
	}

	void CoreLayer::onJobRetryRequested(const JobRetryRequested &event)
	{
		if (m_JobSystem == nullptr || event.id == 0)
			return;

		(void)m_JobSystem->Retry(event.id, event.priority);
	}

	void CoreLayer::onJobHistoryRemoveRequested(const JobHistoryRemoveRequested &event)
	{
		if (m_JobSystem == nullptr)
			return;

		for (const JobId id : event.ids)
		{
			if (m_JobSystem->RemoveFromHistory(id) && m_ProgressTracker != nullptr)
				(void)m_ProgressTracker->RemoveEntry(id);
		}
	}

	void CoreLayer::onBindingsLoaded(const AppEvents::Keymap::BindingsLoaded &event)
	{
		if (m_KeymapResolver == nullptr)
			return;

		for (const KeyBinding &binding : event.bindings)
		{
			if (binding.id.rfind("system.", 0) == 0)
				continue;
			auto result = m_KeymapResolver->RegisterBinding(binding);
			if (!result)
				DS_LOG_WARN("CoreLayer: loaded binding conflict: {}", result.Error().technicalDetails);
		}
		DS_LOG_INFO("CoreLayer: {} user keybindings loaded", event.bindings.size());
	}

	bool CoreLayer::onKeyPressed(KeyPressedEvent &event)
	{
		if (m_CommandService == nullptr || m_KeymapResolver == nullptr || m_ContextManager == nullptr)
			return false;

		const KeyChord chord{static_cast<KeyCode>(event.GetKeyCode()), currentKeyModifiers()};
		KeyInputProcessor processor(*m_KeymapResolver, *m_ContextManager);
		auto inputResult = processor.HandleKeyPressed(chord);
		if (!inputResult)
		{
			DS_LOG_WARN("CoreLayer key input failed: {}", inputResult.Error().technicalDetails);
			return false;
		}

		if (!inputResult->handled || !inputResult->commandId)
			return false;

		CommandContext context(ContextID{"core.keybinding"});
		context.SetSource("CoreLayer shortcut " + ToString(chord));
		auto commandResult = m_CommandService->Execute(*inputResult->commandId, std::move(context));
		if (!commandResult)
			DS_LOG_WARN("CoreLayer command shortcut failed: {}", commandResult.Error().technicalDetails);

		return true;
	}

	Unique<ICommand> CoreLayer::createUndoCommand(CommandContext &)
	{
		return CreateUnique<UndoCommand>(m_UndoStack);
	}

	Unique<ICommand> CoreLayer::createRedoCommand(CommandContext &)
	{
		return CreateUnique<RedoCommand>(m_UndoStack);
	}

	Unique<ICommand> CoreLayer::createOpenCommandPaletteCommand(CommandContext &)
	{
		return CreateUnique<OpenCommandPaletteCommand>(m_EventBus);
	}

	Unique<ICommand> CoreLayer::createQuitCommand(CommandContext &)
	{
		return CreateUnique<QuitCommand>(m_EventBus);
	}

	Unique<ICommand> CoreLayer::createSaveProjectCommand(CommandContext &)
	{
		return CreateUnique<SaveProjectCommand>(m_EventBus);
	}

	void CoreLayer::registerCommand(CommandMeta meta, CommandFactory factory)
	{
		if (m_CommandRegistry == nullptr)
			return;
		auto result = m_CommandRegistry->Register(std::move(meta), std::move(factory));
		if (!result)
			DS_LOG_ERROR("CoreLayer: failed to register command: {}", result.Error().technicalDetails);
	}

	void CoreLayer::registerBinding(KeyBinding binding)
	{
		if (m_KeymapResolver == nullptr)
			return;
		auto result = m_KeymapResolver->RegisterBinding(std::move(binding));
		if (!result)
			DS_LOG_WARN("CoreLayer: keybinding conflict: {}", result.Error().technicalDetails);
	}

	void CoreLayer::registerSystemCommands()
	{
		if (m_CommandRegistry == nullptr || m_KeymapResolver == nullptr)
			return;

		registerCommand(
			CommandMeta{
				CommandID{"edit.undo"},
				"Undo",
				"Edit",
				"Undo the last action.",
				{},
				CommandFlags::HiddenFromPalette},
			std::bind_front(&CoreLayer::createUndoCommand, this));

		registerCommand(
			CommandMeta{
				CommandID{"edit.redo"},
				"Redo",
				"Edit",
				"Redo the last undone action.",
				{},
				CommandFlags::HiddenFromPalette},
			std::bind_front(&CoreLayer::createRedoCommand, this));

		registerCommand(
			CommandMeta{
				CommandID{"app.command_palette"},
				"Command Palette",
				"Application",
				"Open the command palette.",
				{},
				CommandFlags::None},
			std::bind_front(&CoreLayer::createOpenCommandPaletteCommand, this));

		registerCommand(
			CommandMeta{
				CommandID{"app.quit"},
				"Quit",
				"Application",
				"Quit the application.",
				{},
				CommandFlags::None},
			std::bind_front(&CoreLayer::createQuitCommand, this));

		registerCommand(
			CommandMeta{
				CommandID{"project.save"},
				"Save",
				"Project",
				"Save the current project.",
				{},
				CommandFlags::None},
			std::bind_front(&CoreLayer::createSaveProjectCommand, this));

		registerBinding({
			"system.undo",
			KeyChord{KeyCode::Z, KeyModifiers::Ctrl},
			CommandID{"edit.undo"},
			{},
			KeymapLayer::Global,
			true});

		registerBinding({
			"system.redo",
			KeyChord{KeyCode::Y, KeyModifiers::Ctrl},
			CommandID{"edit.redo"},
			{},
			KeymapLayer::Global,
			true});

		registerBinding({
			"system.command_palette",
			KeyChord{KeyCode::P, KeyModifiers::Ctrl | KeyModifiers::Shift},
			CommandID{"app.command_palette"},
			{},
			KeymapLayer::Global,
			true});

		registerBinding({
			"system.quit",
			KeyChord{KeyCode::W, KeyModifiers::Ctrl | KeyModifiers::Shift},
			CommandID{"app.quit"},
			{},
			KeymapLayer::Global,
			true});

		registerBinding({
			"system.save",
			KeyChord{KeyCode::S, KeyModifiers::Ctrl},
			CommandID{"project.save"},
			{},
			KeymapLayer::Global,
			true});

		DS_LOG_INFO("CoreLayer: system commands and bindings registered");
	}
} // namespace DefectStudio
