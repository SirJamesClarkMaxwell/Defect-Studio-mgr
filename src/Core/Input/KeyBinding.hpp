#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "Core/Commands/CommandRegistry.hpp"
#include "Core/Diagnostics/StructuredError.hpp"
#include "Core/Utils/KeyCodes.hpp"

namespace DefectStudio
{
	enum class KeyModifiers : std::uint8_t
	{
		None = 0,
		Ctrl = 1u << 0u,
		Shift = 1u << 1u,
		Alt = 1u << 2u,
		Super = 1u << 3u
	};

	[[nodiscard]] constexpr KeyModifiers operator|(KeyModifiers lhs, KeyModifiers rhs) noexcept
	{
		return static_cast<KeyModifiers>(static_cast<std::uint8_t>(lhs) | static_cast<std::uint8_t>(rhs));
	}

	[[nodiscard]] constexpr KeyModifiers operator&(KeyModifiers lhs, KeyModifiers rhs) noexcept
	{
		return static_cast<KeyModifiers>(static_cast<std::uint8_t>(lhs) & static_cast<std::uint8_t>(rhs));
	}

	[[nodiscard]] constexpr bool HasModifier(KeyModifiers modifiers, KeyModifiers modifier) noexcept
	{
		return (modifiers & modifier) != KeyModifiers::None;
	}

	struct KeyChord
	{
		KeyCode key = KeyCode::Unknown;
		KeyModifiers modifiers = KeyModifiers::None;

		[[nodiscard]] bool operator==(const KeyChord &other) const noexcept
		{
			return key == other.key && modifiers == other.modifiers;
		}
	};

	class ContextManager
	{
	public:
		void SetActive(std::string context, bool active = true);
		void Clear();

		[[nodiscard]] bool IsActive(const std::string &context) const;

	private:
		std::vector<std::string> m_ActiveContexts;
	};

	class ContextExpr
	{
	public:
		ContextExpr() = default;
		explicit ContextExpr(std::string expression);

		[[nodiscard]] bool Matches(const ContextManager &contextManager) const;
		[[nodiscard]] const std::string &GetExpression() const noexcept;
		[[nodiscard]] bool Empty() const noexcept;

	private:
		std::string m_Expression;
	};

	enum class KeymapLayer : std::uint8_t
	{
		Global = 0,
		Project = 1,
		WindowLocal = 2
	};

	enum class KeyBindingConflictType
	{
		ExactChordAndContext,
		PrefixReserved
	};

	struct KeyBinding
	{
		std::string id;
		KeyChord chord;
		CommandID commandId;
		ContextExpr when;
		KeymapLayer layer = KeymapLayer::Global;
		bool enabled = true;
	};

	struct KeyBindingConflict
	{
		KeyBinding existingBinding;
		KeyBinding newBinding;
		KeyBindingConflictType type = KeyBindingConflictType::ExactChordAndContext;
	};
	
	struct RegisteredBinding
	{
		KeyBinding binding;
		std::size_t order = 0;
	};
	class KeymapResolver
	{
	public:
		[[nodiscard]] Result<void> RegisterBinding(KeyBinding binding);
		void Clear();

		[[nodiscard]] std::optional<KeyBinding> Resolve(const KeyChord &chord, const ContextManager &contextManager) const;
		[[nodiscard]] std::vector<KeyBinding> ListBindings() const;
		[[nodiscard]] const std::vector<KeyBindingConflict> &GetConflicts() const noexcept;

	private:

		[[nodiscard]] bool HasConflict(const KeyBinding &binding, KeyBindingConflict &conflict) const;

		std::vector<RegisteredBinding> m_Bindings;
		std::vector<KeyBindingConflict> m_Conflicts;
		std::size_t m_NextOrder = 1;
	};

	struct KeyInputResult
	{
		bool handled = false;
		std::optional<CommandID> commandId;
	};

	class KeyInputProcessor
	{
	public:
		KeyInputProcessor(CommandRegistry &commandRegistry, KeymapResolver &resolver, ContextManager &contextManager);

		[[nodiscard]] Result<KeyInputResult> HandleKeyPressed(const KeyChord &chord, CommandContext context = {});

	private:
		CommandRegistry &m_CommandRegistry;
		KeymapResolver &m_Resolver;
		ContextManager &m_ContextManager;
	};

	[[nodiscard]] std::string ToString(const KeyChord &chord);
} // namespace DefectStudio
