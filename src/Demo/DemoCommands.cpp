#include "Core/dspch.hpp"

#include <string>
#include <utility>

#include "Demo/DemoCommands.hpp"

namespace DefectStudio::Demo
{
	DemoValueDeltaCommand::DemoValueDeltaCommand(int &value, int delta, std::string description)
		: m_Value(&value), m_Delta(delta), m_Description(std::move(description))
	{
	}

	Result<void> DemoValueDeltaCommand::Execute(CommandContext &context)
	{
		(void)context;
		*m_Value += m_Delta;
		return {};
	}

	Result<void> DemoValueDeltaCommand::Undo(CommandContext &context)
	{
		(void)context;
		*m_Value -= m_Delta;
		return {};
	}

	std::string DemoValueDeltaCommand::Description() const
	{
		return m_Description + " (" + std::to_string(m_Delta) + ")";
	}

	bool DemoValueDeltaCommand::IsUndoable() const noexcept
	{
		return true;
	}

	bool DemoValueDeltaCommand::CanMerge(const ICommand &next) const noexcept
	{
		const auto *nextDelta = dynamic_cast<const DemoValueDeltaCommand *>(&next);
		return nextDelta != nullptr && nextDelta->m_Value == m_Value && nextDelta->m_Description == m_Description;
	}

	Result<void> DemoValueDeltaCommand::Merge(std::unique_ptr<ICommand> next)
	{
		auto *nextDelta = dynamic_cast<DemoValueDeltaCommand *>(next.get());
		if (nextDelta == nullptr)
		{
			return StructuredError{
				ErrorCategory::Validation,
				Severity::Error,
				"Demo command merge failed.",
				"UndoStack attempted to merge incompatible demo commands.",
				"Keep mergeable command families type-compatible.",
				"DemoLayer",
				"demo.command.merge_mismatch"};
		}

		m_Delta += nextDelta->m_Delta;
		return {};
	}

	DemoSetValueCommand::DemoSetValueCommand(int &value, int newValue, std::string description)
		: m_Value(&value), m_NewValue(newValue), m_Description(std::move(description))
	{
	}

	Result<void> DemoSetValueCommand::Execute(CommandContext &context)
	{
		(void)context;
		m_OldValue = *m_Value;
		*m_Value = m_NewValue;
		return {};
	}

	Result<void> DemoSetValueCommand::Undo(CommandContext &context)
	{
		(void)context;
		*m_Value = m_OldValue;
		return {};
	}

	Result<void> DemoSetValueCommand::Redo(CommandContext &context)
	{
		(void)context;
		*m_Value = m_NewValue;
		return {};
	}

	std::string DemoSetValueCommand::Description() const
	{
		return m_Description;
	}

	bool DemoSetValueCommand::IsUndoable() const noexcept
	{
		return true;
	}

	DemoSetBoolCommand::DemoSetBoolCommand(bool &value, bool newValue, std::string description)
		: m_Value(&value), m_NewValue(newValue), m_Description(std::move(description))
	{
	}

	Result<void> DemoSetBoolCommand::Execute(CommandContext &context)
	{
		(void)context;
		m_OldValue = *m_Value;
		*m_Value = m_NewValue;
		return {};
	}

	Result<void> DemoSetBoolCommand::Undo(CommandContext &context)
	{
		(void)context;
		*m_Value = m_OldValue;
		return {};
	}

	Result<void> DemoSetBoolCommand::Redo(CommandContext &context)
	{
		(void)context;
		*m_Value = m_NewValue;
		return {};
	}

	std::string DemoSetBoolCommand::Description() const
	{
		return m_Description;
	}

	bool DemoSetBoolCommand::IsUndoable() const noexcept
	{
		return true;
	}

	DemoAppendTokenCommand::DemoAppendTokenCommand(std::vector<std::string> &tokens, std::string token)
		: m_Tokens(&tokens), m_Token(std::move(token))
	{
	}

	Result<void> DemoAppendTokenCommand::Execute(CommandContext &context)
	{
		(void)context;
		m_Tokens->push_back(m_Token);
		return {};
	}

	Result<void> DemoAppendTokenCommand::Undo(CommandContext &context)
	{
		(void)context;
		if (!m_Tokens->empty())
			m_Tokens->pop_back();
		return {};
	}

	Result<void> DemoAppendTokenCommand::Redo(CommandContext &context)
	{
		(void)context;
		m_Tokens->push_back(m_Token);
		return {};
	}

	std::string DemoAppendTokenCommand::Description() const
	{
		return "Append token " + m_Token;
	}

	bool DemoAppendTokenCommand::IsUndoable() const noexcept
	{
		return true;
	}

	DemoClearTokensCommand::DemoClearTokensCommand(std::vector<std::string> &tokens)
		: m_Tokens(&tokens)
	{
	}

	Result<void> DemoClearTokensCommand::Execute(CommandContext &context)
	{
		(void)context;
		m_PreviousTokens = *m_Tokens;
		m_Tokens->clear();
		return {};
	}

	Result<void> DemoClearTokensCommand::Undo(CommandContext &context)
	{
		(void)context;
		*m_Tokens = m_PreviousTokens;
		return {};
	}

	Result<void> DemoClearTokensCommand::Redo(CommandContext &context)
	{
		(void)context;
		m_Tokens->clear();
		return {};
	}

	std::string DemoClearTokensCommand::Description() const
	{
		return "Clear demo tokens";
	}

	bool DemoClearTokensCommand::IsUndoable() const noexcept
	{
		return true;
	}

	DemoCallbackCommand::DemoCallbackCommand(std::string description, std::function<Result<void>(CommandContext &)> callback)
		: m_Description(std::move(description)), m_Callback(std::move(callback))
	{
	}

	Result<void> DemoCallbackCommand::Execute(CommandContext &context)
	{
		return m_Callback(context);
	}

	std::string DemoCallbackCommand::Description() const
	{
		return m_Description;
	}
} // namespace DefectStudio::Demo
