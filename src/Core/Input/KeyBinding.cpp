#include "Core/dspch.hpp"

#include "Core/Input/KeyBinding.hpp"

#include <sstream>

namespace DefectStudio
{
	namespace
	{
		[[nodiscard]] std::string trim(std::string value)
		{
			const auto first = std::find_if_not(value.begin(), value.end(), [](unsigned char ch) {
				return std::isspace(ch) != 0;
			});
			const auto last = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char ch) {
				return std::isspace(ch) != 0;
			}).base();

			if (first >= last)
				return {};
			return std::string(first, last);
		}

		[[nodiscard]] std::vector<std::string> splitAndExpression(const std::string &expression)
		{
			std::vector<std::string> tokens;
			std::size_t start = 0;
			while (start < expression.size())
			{
				const std::size_t next = expression.find("&&", start);
				const std::size_t end = next == std::string::npos ? expression.size() : next;
				tokens.push_back(trim(expression.substr(start, end - start)));
				if (next == std::string::npos)
					break;
				start = next + 2;
			}
			return tokens;
		}

		[[nodiscard]] bool contextsOverlap(const ContextExpr &lhs, const ContextExpr &rhs)
		{
			return lhs.Empty() || rhs.Empty() || lhs.GetExpression() == rhs.GetExpression();
		}

		[[nodiscard]] int layerPriority(KeymapLayer layer)
		{
			return static_cast<int>(layer);
		}

		[[nodiscard]] std::string keyName(KeyCode key)
		{
			const auto native = static_cast<int>(key);
			if (key >= KeyCode::A && key <= KeyCode::Z)
				return std::string(1, static_cast<char>(native));
			if (key >= KeyCode::D0 && key <= KeyCode::D9)
				return std::string(1, static_cast<char>(native));

			switch (key)
			{
			case KeyCode::Space:
				return "Space";
			case KeyCode::Enter:
				return "Enter";
			case KeyCode::Tab:
				return "Tab";
			case KeyCode::Escape:
				return "Esc";
			case KeyCode::Delete:
				return "Delete";
			case KeyCode::Backspace:
				return "Backspace";
			case KeyCode::Left:
				return "Left";
			case KeyCode::Right:
				return "Right";
			case KeyCode::Up:
				return "Up";
			case KeyCode::Down:
				return "Down";
			case KeyCode::F1:
			case KeyCode::F2:
			case KeyCode::F3:
			case KeyCode::F4:
			case KeyCode::F5:
			case KeyCode::F6:
			case KeyCode::F7:
			case KeyCode::F8:
			case KeyCode::F9:
			case KeyCode::F10:
			case KeyCode::F11:
			case KeyCode::F12:
				return "F" + std::to_string(native - static_cast<int>(KeyCode::F1) + 1);
			default:
				return std::to_string(native);
			}
		}

		[[nodiscard]] StructuredError makeKeymapError(
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
	}

	void ContextManager::SetActive(std::string context, bool active)
	{
		auto it = std::find(m_ActiveContexts.begin(), m_ActiveContexts.end(), context);
		if (active)
		{
			if (it == m_ActiveContexts.end())
				m_ActiveContexts.push_back(std::move(context));
			return;
		}

		if (it != m_ActiveContexts.end())
			m_ActiveContexts.erase(it);
	}

	void ContextManager::Clear()
	{
		m_ActiveContexts.clear();
	}

	bool ContextManager::IsActive(const std::string &context) const
	{
		return std::find(m_ActiveContexts.begin(), m_ActiveContexts.end(), context) != m_ActiveContexts.end();
	}

	ContextExpr::ContextExpr(std::string expression)
		: m_Expression(trim(std::move(expression)))
	{
	}

	bool ContextExpr::Matches(const ContextManager &contextManager) const
	{
		if (m_Expression.empty())
			return true;

		const std::vector<std::string> tokens = splitAndExpression(m_Expression);
		for (std::string token : tokens)
		{
			if (token.empty())
				continue;

			bool negated = false;
			if (token.front() == '!')
			{
				negated = true;
				token = trim(token.substr(1));
			}

			const bool active = contextManager.IsActive(token);
			if (negated == active)
				return false;
		}
		return true;
	}

	const std::string &ContextExpr::GetExpression() const noexcept
	{
		return m_Expression;
	}

	bool ContextExpr::Empty() const noexcept
	{
		return m_Expression.empty();
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

		KeyBindingConflict conflict;
		if (HasConflict(binding, conflict))
		{
			m_Conflicts.push_back(conflict);
			return makeKeymapError(
				"keymap.register.conflict",
				"Key binding conflicts with an existing shortcut.",
				"Binding '" + binding.id + "' conflicts with '" + conflict.existingBinding.id + "' on " + ToString(binding.chord) + ".",
				"Move one binding to a narrower context or choose a different chord.");
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

	const std::vector<KeyBindingConflict> &KeymapResolver::GetConflicts() const noexcept
	{
		return m_Conflicts;
	}

	bool KeymapResolver::HasConflict(const KeyBinding &binding, KeyBindingConflict &conflict) const
	{
		for (const RegisteredBinding &registered : m_Bindings)
		{
			const KeyBinding &existing = registered.binding;
			if (existing.layer != binding.layer || !(existing.chord == binding.chord))
				continue;

			if (!contextsOverlap(existing.when, binding.when))
				continue;

			conflict = KeyBindingConflict{existing, binding, KeyBindingConflictType::ExactChordAndContext};
			return true;
		}
		return false;
	}

	KeyInputProcessor::KeyInputProcessor(CommandRegistry &commandRegistry, KeymapResolver &resolver, ContextManager &contextManager)
		: m_CommandRegistry(commandRegistry), m_Resolver(resolver), m_ContextManager(contextManager)
	{
	}

	Result<KeyInputResult> KeyInputProcessor::HandleKeyPressed(const KeyChord &chord, CommandContext context)
	{
		std::optional<KeyBinding> binding = m_Resolver.Resolve(chord, m_ContextManager);
		if (!binding)
			return KeyInputResult{};

		if (context.GetSource().empty())
			context.SetSource("KeyInputProcessor");

		auto result = m_CommandRegistry.Execute(binding->commandId, std::move(context));
		if (!result)
			return result.Error();

		KeyInputResult inputResult;
		inputResult.handled = true;
		inputResult.commandId = binding->commandId;
		return inputResult;
	}

	std::string ToString(const KeyChord &chord)
	{
		std::ostringstream out;
		if (HasModifier(chord.modifiers, KeyModifiers::Ctrl))
			out << "Ctrl+";
		if (HasModifier(chord.modifiers, KeyModifiers::Shift))
			out << "Shift+";
		if (HasModifier(chord.modifiers, KeyModifiers::Alt))
			out << "Alt+";
		if (HasModifier(chord.modifiers, KeyModifiers::Super))
			out << "Super+";
		out << keyName(chord.key);
		return out.str();
	}
} // namespace DefectStudio
