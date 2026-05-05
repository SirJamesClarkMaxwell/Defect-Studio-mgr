#pragma once

#include <array>
#include <string>
#include <vector>

#include "Core/Input/KeyBinding.hpp"
#include "Core/Utils/Memory.hpp"

namespace DefectStudio
{
	class AssetManager;
	class CapabilityService;
	class CommandPaletteIndex;
	class CommandRegistry;
	class CommandService;
	class CommandContext;
	class EventBus;
	class Event;
	class ICommand;
	class KeyPressedEvent;
	class KeymapResolver;
	struct Notification;
	class UndoStack;
}

namespace DefectStudio::Demo
{
	class DemoBackendRuntime
	{
	public:
		DemoBackendRuntime(Ref<CapabilityService> capabilityService, Ref<EventBus> eventBus, WeakRef<AssetManager> assetManager = {});
		~DemoBackendRuntime();

		void OnEvent(Event &event);
		void Render();

	private:
		void setupBackendRuntimeDemo();
		[[nodiscard]] Unique<ICommand> createIncrementCommand(CommandContext &context);
		[[nodiscard]] Unique<ICommand> createDecrementCommand(CommandContext &context);
		[[nodiscard]] Unique<ICommand> createResetCommand(CommandContext &context);
		[[nodiscard]] Unique<ICommand> createUndoCommand(CommandContext &context);
		[[nodiscard]] Unique<ICommand> createRedoCommand(CommandContext &context);
		[[nodiscard]] Unique<ICommand> createNotifyCommand(CommandContext &context);
		[[nodiscard]] Unique<ICommand> createMissingCapabilityCommand(CommandContext &context);
		[[nodiscard]] Unique<ICommand> createSetSliderCommand(CommandContext &context);
		[[nodiscard]] Unique<ICommand> createSetFlagCommand(CommandContext &context);
		[[nodiscard]] Unique<ICommand> createAppendTokenCommand(CommandContext &context);
		[[nodiscard]] Unique<ICommand> createClearTokensCommand(CommandContext &context);
		bool onBackendDemoKeyPressed(KeyPressedEvent &event);
		bool executeBackendDemoChord(const KeyChord &chord, const char *source);
		void renderUndoRedoExamples();
		void appendBackendRuntimeLog(std::string message);
		void requestNotification(Notification notification);

	private:
		Ref<CapabilityService> m_CapabilityService;
		Ref<EventBus> m_EventBus;
		WeakRef<AssetManager> m_AssetManager;
		int m_BackendDemoValue = 0;
		int m_BackendSliderValue = 50;
		int m_PendingSliderValue = 50;
		int m_BackendTokenCounter = 0;
		bool m_BackendFlag = false;
		bool m_PendingFlagValue = false;
		bool m_BackendSliderGroupActive = false;
		std::string m_PendingToken;
		std::vector<std::string> m_BackendTokens;
		bool m_BackendHotkeysEnabled = true;
		std::array<char, 96> m_CommandPaletteSearch{};
		std::vector<std::string> m_BackendRuntimeLog;

		Ref<UndoStack> m_BackendUndoStack;
		Ref<CommandRegistry> m_BackendCommandRegistry;
		Unique<CommandService> m_BackendCommandService;
		Unique<KeymapResolver> m_BackendKeymapResolver;
		Unique<ContextManager> m_BackendContextManager;
		Unique<CommandPaletteIndex> m_BackendCommandPalette;
	};
} // namespace DefectStudio::Demo
