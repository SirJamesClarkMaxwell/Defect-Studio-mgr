#include "Core/dspch.hpp"

#include "Core/Input/KeymapResolver.hpp"

#include "Core/Input/ContextManager.hpp"
#include "Core/Logging/Logger.hpp"

namespace DefectStudio
{
	[[nodiscard]] static int layerPriority(KeymapLayer layer)
	{
		return static_cast<int>(layer);
	}

	[[nodiscard]] static StructuredError makeKeymapError(
		std::string code,
		std::string userMessage,
		std::string technicalDetails,
		std::string suggestion)
	{
		return StructuredError{
			ErrorCategory::Validation,
			Severity::Error,
			std::move(userMessage),
			std::move(technicalDetails),
			std::move(suggestion),
			"KeymapResolver",
			std::move(code)};
	}

	Result<void> KeymapResolver::RegisterBinding(KeyBinding binding)
	{
		if (binding.id.empty())
		{
			return makeKeymapError(
				"keymap.register.empty_id",
				"Key binding registration failed.",
				"Binding id is empty.",
				"Provide a stable binding id for diagnostics and conflict reports.");
		}

		if (!binding.commandId)
		{
			return makeKeymapError(
				"keymap.register.empty_command",
				"Key binding registration failed.",
				"Binding '" + binding.id + "' has an empty command id.",
				"Bind shortcuts only to registered command ids.");
		}

		if (binding.chord.key == KeyCode::Unknown)
		{
			return makeKeymapError(
				"keymap.register.empty_key",
				"Key binding registration failed.",
				"Binding '" + binding.id + "' has an unknown key.",
				"Normalize platform input before registering bindings.");
		}

		for (const RegisteredBinding &registered : m_Bindings)
		{
			const KeyBinding &existing = registered.binding;
			if (existing.chord == binding.chord
				&& existing.when.GetExpression() == binding.when.GetExpression()
				&& existing.layer == binding.layer)
			{
				m_Conflicts.push_back({existing, binding});
				DS_LOG_WARN(
					"KeyBinding conflict: '{}' conflicts with '{}' on chord '{}'",
					binding.id,
					existing.id,
					ToString(binding.chord));
				return makeKeymapError(
					"keymap.register.conflict",
					"Key binding registration failed.",
					"Binding '" + binding.id + "' conflicts with '" + existing.id + "' on chord '" + ToString(binding.chord) + "'.",
					"Use a different chord or context for one of the bindings.");
			}
		}

		m_Bindings.push_back(RegisteredBinding{std::move(binding), m_NextOrder++});
		return {};
	}

	void KeymapResolver::Clear()
	{
		m_Bindings.clear();
		m_Conflicts.clear();
		m_NextOrder = 1;
	}

	std::optional<KeyBinding> KeymapResolver::Resolve(const KeyChord &chord, const ContextManager &contextManager) const
	{
		const RegisteredBinding *best = nullptr;
		for (const RegisteredBinding &registered : m_Bindings)
		{
			const KeyBinding &binding = registered.binding;
			if (!binding.enabled || !(binding.chord == chord) || !binding.when.Matches(contextManager))
				continue;

			if (best == nullptr
				|| layerPriority(binding.layer) > layerPriority(best->binding.layer)
				|| (binding.layer == best->binding.layer && registered.order > best->order))
			{
				best = &registered;
			}
		}

		if (best == nullptr)
			return std::nullopt;
		return best->binding;
	}

	std::vector<KeyBinding> KeymapResolver::ListBindings() const
	{
		std::vector<KeyBinding> bindings;
		bindings.reserve(m_Bindings.size());
		for (const RegisteredBinding &registered : m_Bindings)
			bindings.push_back(registered.binding);
		return bindings;
	}

	std::vector<KeyBinding> KeymapResolver::GetAllBindings() const
	{
		return ListBindings();
	}

	const std::vector<KeyBindingConflict> &KeymapResolver::GetConflicts() const noexcept
	{
		return m_Conflicts;
	}
} // namespace DefectStudio
