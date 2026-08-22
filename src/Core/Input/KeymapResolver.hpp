#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "Core/Diagnostics/StructuredError.hpp"
#include "Core/Input/ContextExpr.hpp"
#include "Core/Input/KeyChord.hpp"
#include "Core/Types/CoreIds.hpp"

namespace DefectStudio
{
	enum class KeymapLayer : std::uint8_t
	{
		Global = 0,
		Project = 1,
		WindowLocal = 2
	};

	struct KeyBinding
	{
		std::string id;
		KeyChord chord;
		CommandID commandId;
		ContextExpr when;
		KeymapLayer layer = KeymapLayer::Global;
		bool enabled = true;
		// When true, holding the chord down fires the command on every OS key-repeat (GLFW_REPEAT),
		// not just the initial press - see CoreLayer::onKeyRepeated. Off by default: most commands
		// (delete, duplicate, toggle-X) would be actively harmful to fire repeatedly from one held
		// key, so this is opt-in per-binding for the few that want continuous behaviour (camera
		// orbit while held, held-move nudge, ...).
		bool repeatable = false;
	};

	struct KeyBindingConflict
	{
		KeyBinding existingBinding;
		KeyBinding newBinding;
	};

	struct RegisteredBinding
	{
		KeyBinding binding;
		std::size_t order = 0;
	};

	class ContextManager;

	class KeymapResolver
	{
	public:
		[[nodiscard]] Result<void> RegisterBinding(KeyBinding binding);
		void Clear();

		[[nodiscard]] std::optional<KeyBinding> Resolve(const KeyChord &chord, const ContextManager &contextManager) const;
		[[nodiscard]] std::vector<KeyBinding> ListBindings() const;
		[[nodiscard]] std::vector<KeyBinding> GetAllBindings() const;
		[[nodiscard]] const std::vector<KeyBindingConflict> &GetConflicts() const noexcept;

	private:
		std::vector<RegisteredBinding> m_Bindings;
		std::vector<KeyBindingConflict> m_Conflicts;
		std::size_t m_NextOrder = 1;
	};
} // namespace DefectStudio
