#pragma once

#include <array>
#include <string>
#include <vector>

#include "Core/Input/KeyBinding.hpp"
#include "Core/Utils/Memory.hpp"

namespace DefectStudio
{
	class CapabilityService;
	class CommandPaletteIndex;
	class CommandRegistry;
	class EventBus;
	class Event;
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
		DemoBackendRuntime(Ref<CapabilityService> capabilityService, Ref<EventBus> eventBus);
		~DemoBackendRuntime();

		void OnEvent(Event &event);
		void Render();

	private:
		void setupBackendRuntimeDemo();
		bool onBackendDemoKeyPressed(KeyPressedEvent &event);
		bool executeBackendDemoChord(const KeyChord &chord, const char *source);
		void appendBackendRuntimeLog(std::string message);
		void requestNotification(Notification notification);

	private:
		Ref<CapabilityService> m_CapabilityService;
		Ref<EventBus> m_EventBus;
		int m_BackendDemoValue = 0;
		bool m_BackendHotkeysEnabled = true;
		std::array<char, 96> m_CommandPaletteSearch{};
		std::vector<std::string> m_BackendRuntimeLog;

		Unique<UndoStack> m_BackendUndoStack;
		Unique<CommandRegistry> m_BackendCommandRegistry;
		Unique<KeymapResolver> m_BackendKeymapResolver;
		Unique<ContextManager> m_BackendContextManager;
		Unique<CommandPaletteIndex> m_BackendCommandPalette;
	};
} // namespace DefectStudio::Demo
