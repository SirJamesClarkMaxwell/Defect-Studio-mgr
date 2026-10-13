#include "Core/dspch.hpp"

#include "Core/CoreLayer.hpp"
#include "Core/EventSystem/DispatchingEventSystem/PlatformEvents/KeyboardEvents.hpp"
#include "Core/Logging/Logger.hpp"
#include "Core/Utils/Input.hpp"

namespace DefectStudio
{
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
		return dispatchKeyChord(Input::MakeKeyChord(static_cast<KeyCode>(event.GetKeyCode())), false);
	}

	// Only bindings explicitly marked repeatable fire on OS key repeat.
	bool CoreLayer::onKeyRepeated(KeyRepeatedEvent &event)
	{
		return dispatchKeyChord(Input::MakeKeyChord(static_cast<KeyCode>(event.GetKeyCode())), true);
	}

	bool CoreLayer::dispatchKeyChord(const KeyChord &chord, bool requireRepeatable)
	{
		if (m_CommandService == nullptr || m_KeymapResolver == nullptr || m_ContextManager == nullptr)
			return false;

		std::optional<KeyBinding> binding = m_KeymapResolver->Resolve(chord, *m_ContextManager);
		if (!binding)
			return false;
		if (requireRepeatable && !binding->repeatable)
			return false;
		// The viewport handles Shift+D as an in-place duplicate gesture; keep the view command in the palette.
		if (chord == KeyChord{KeyCode::D, KeyModifiers::Shift} &&
			binding->commandId.value == "renderer.view.set_as_project_default")
			return true;

		CommandContext context(ContextID{"core.keybinding"});
		context.SetSource("CoreLayer shortcut " + ToString(chord));
		const std::string &id = binding->commandId.value;
		const CommandID sceneShortcut{"renderer.scene_objects.shortcut"};
		if ((id == "renderer.selection.delete" || id == "renderer.selection.copy" ||
			id == "renderer.selection.paste" || id == "renderer.selection.duplicate") &&
			m_CommandService->HasCommand(sceneShortcut))
		{
			// Presentation owns scene selection; let its registered command claim the edit before atoms.
			const Ref<bool> handled = CreateRef<bool>(false);
			context.Set("scene_objects.shortcut_action", id);
			context.Set("scene_objects.shortcut_handled", handled);
			const auto routed = m_CommandService->Execute(sceneShortcut, context);
			if (!routed) DS_LOG_WARN("CoreLayer scene shortcut failed: {}", routed.Error().technicalDetails);
			if (!routed || *handled) return true;
		}
		auto commandResult = m_CommandService->Execute(binding->commandId, std::move(context));
		if (!commandResult)
			DS_LOG_WARN("CoreLayer command shortcut failed: {}", commandResult.Error().technicalDetails);

		return true;
	}
}
