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

	class ContextManager;

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
} // namespace DefectStudio
