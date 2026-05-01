#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "Core/Input/KeyBinding.hpp"
#include "Core/Layer.hpp"
#include "Core/Utils/Memory.hpp"

namespace DefectStudio
{
	class CommandPaletteIndex;
	class CommandRegistry;
	class EventBus;
	class KeyPressedEvent;
	class KeymapResolver;
	class UndoStack;
}

namespace DefectStudio::Demo
{
	class EventDispatcherDemo;
	class EventBusDemo;
	class JobSystemDemo;

	class DemoLayer final : public Layer
	{
	public:
		DemoLayer();
		~DemoLayer();

		void OnAttach() override;
		void OnDetach() override;
		void OnEvent(Event &event) override;
		void OnImGuiRender() override;

	private:
		void renderNotificationDemo();
		void setupBackendRuntimeDemo();
		void renderBackendRuntimeDemo();
		bool onBackendDemoKeyPressed(KeyPressedEvent &event);
		bool executeBackendDemoChord(const KeyChord &chord, const char *source);
		void appendBackendRuntimeLog(std::string message);

	private:
		std::uint32_t m_NotificationDemoCounter = 0;
		int m_BackendDemoValue = 0;
		bool m_BackendHotkeysEnabled = true;
		std::array<char, 96> m_CommandPaletteSearch{};
		std::vector<std::string> m_BackendRuntimeLog;

		Ref<EventBus> m_DemoEventBus;
		Unique<EventDispatcherDemo> m_EventDispatcherDemo;
		Unique<EventBusDemo> m_EventBusDemo;
		Unique<JobSystemDemo> m_JobSystemDemo;
		Unique<UndoStack> m_BackendUndoStack;
		Unique<CommandRegistry> m_BackendCommandRegistry;
		Unique<KeymapResolver> m_BackendKeymapResolver;
		Unique<ContextManager> m_BackendContextManager;
		Unique<CommandPaletteIndex> m_BackendCommandPalette;
	};
} // namespace DefectStudio::Demo
