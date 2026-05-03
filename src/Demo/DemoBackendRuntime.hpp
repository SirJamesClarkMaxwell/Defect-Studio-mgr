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
	class Event;
	class KeyPressedEvent;
	class KeymapResolver;
	class UndoStack;
}

namespace DefectStudio::Demo
{
	class DemoBackendRuntime
	{
	public:
		explicit DemoBackendRuntime(CapabilityService *capabilityService);
		~DemoBackendRuntime();

		void OnEvent(Event &event);
		void Render();

	private:
		void setupBackendRuntimeDemo();
		bool onBackendDemoKeyPressed(KeyPressedEvent &event);
		bool executeBackendDemoChord(const KeyChord &chord, const char *source);
		void appendBackendRuntimeLog(std::string message);

	private:
		CapabilityService *m_CapabilityService = nullptr; 
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
