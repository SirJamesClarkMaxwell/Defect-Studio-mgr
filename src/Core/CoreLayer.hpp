#pragma once

#include "Core/Commands/CommandRegistry.hpp"
#include "Core/Commands/CommandService.hpp"
#include "Core/Input/KeyBinding.hpp"
#include "Core/Undo/UndoStack.hpp"
#include "Core/Utils/Memory.hpp"
#include "App/Events/KeyBindingEvents.hpp"
#include "Core/Layer.hpp"
#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Core/EventSystem/BusEventSystem/EventReceiver.hpp"
#include "Core/JobSystem/JobSystem.hpp"
#include "Core/JobSystem/JobSystemConfig.hpp"
#include "Core/ProgressTrackingSystem/ProgressTracker.hpp"

namespace DefectStudio
{
	struct JobSystemConfigAppliedEvent;
	struct JobCancelRequested;
	struct JobHistoryRemoveRequested;
	struct JobResetRequested;
	struct JobResumeRequested;
	struct JobRetryRequested;
	struct JobSubmitRequested;
	class KeyPressedEvent;

	class CoreLayer final : public Layer, public EventReceiver
	{
	public:
		CoreLayer();

		void OnAttach() override;
		void OnDetach() override;
		void OnUpdate(float deltaTime) override;
		void OnEvent(Event &event) override;

		bool InitializeSystems(Ref<EventBus> eventBus, const JobsConfig &jobsConfig);
		void ShutdownSystems();

		EventBus &GetEventBus();
		JobSystem &GetJobSystem();
		ProgressTracker &GetProgressTracker();
		CommandService &GetCommandService();
		KeymapResolver &GetKeymapResolver();
		ContextManager &GetContextManager();
		UndoStack &GetUndoStack();
		[[nodiscard]] WeakRef<JobSystem> GetJobSystemHandle() const;
		[[nodiscard]] WeakRef<ProgressTracker> GetProgressTrackerHandle() const;
		[[nodiscard]] WeakRef<CommandService> GetCommandServiceHandle() const;
		[[nodiscard]] WeakRef<KeymapResolver> GetKeymapResolverHandle() const;
		[[nodiscard]] WeakRef<ContextManager> GetContextManagerHandle() const;
		[[nodiscard]] WeakRef<CommandRegistry> GetCommandRegistryHandle() const;

	private:
		void applyJobConfig(const JobsConfig &jobsConfig);
		void onJobSystemConfigApplied(const JobSystemConfigAppliedEvent &event);
		void onJobSubmitRequested(const JobSubmitRequested &event);
		void onJobCancelRequested(const JobCancelRequested &event);
		void onJobResumeRequested(const JobResumeRequested &event);
		void onJobResetRequested(const JobResetRequested &event);
		void onJobRetryRequested(const JobRetryRequested &event);
		void onJobHistoryRemoveRequested(const JobHistoryRemoveRequested &event);
		void onBindingsLoaded(const AppEvents::Keymap::BindingsLoaded &event);
		bool onKeyPressed(KeyPressedEvent &event);
		void registerSystemCommands();
		[[nodiscard]] Unique<ICommand> createUndoCommand(CommandContext &context);
		[[nodiscard]] Unique<ICommand> createRedoCommand(CommandContext &context);
		[[nodiscard]] Unique<ICommand> createOpenCommandPaletteCommand(CommandContext &context);
		[[nodiscard]] Unique<ICommand> createQuitCommand(CommandContext &context);
		[[nodiscard]] Unique<ICommand> createSaveProjectCommand(CommandContext &context);
		void registerCommand(CommandMeta meta, CommandFactory factory);
		void registerBinding(KeyBinding binding);

	private:
		float m_AccumulatedTime = 0.0f;
		bool m_SystemsInitialized = false;
		Ref<EventBus> m_EventBus;
		Ref<JobSystem> m_JobSystem;
		Ref<ProgressTracker> m_ProgressTracker;
		Ref<UndoStack> m_UndoStack;
		Ref<CommandRegistry> m_CommandRegistry;
		Ref<CommandService> m_CommandService;
		Ref<KeymapResolver> m_KeymapResolver;
		Ref<ContextManager> m_ContextManager;
	};
} // namespace DefectStudio
