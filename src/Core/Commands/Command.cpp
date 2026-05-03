#include "Core/dspch.hpp"

#include "Core/Commands/Command.hpp"

namespace DefectStudio
{
	Result<void> ICommand::Undo(CommandContext &context)
	{
		(void)context;
		return StructuredError{
			ErrorCategory::Validation,
			Severity::Error,
			"Command cannot be undone.",
			"Undo was requested for a command that did not override ICommand::Undo.",
			"Only commands that return IsUndoable() should be pushed to the undo stack.",
			"ICommand",
			"command.undo.unsupported"};
	}

	Result<void> ICommand::Redo(CommandContext &context)
	{
		return Execute(context);
	}

	bool ICommand::IsUndoable() const noexcept
	{
		return false;
	}

	bool ICommand::CanMerge(const ICommand &next) const noexcept
	{
		(void)next;
		return false;
	}

	Result<void> ICommand::Merge(std::unique_ptr<ICommand> next)
	{
		(void)next;
		return StructuredError{
			ErrorCategory::Validation,
			Severity::Error,
			"Commands cannot be merged.",
			"Merge was requested for a command that did not opt into merge semantics.",
			"Implement CanMerge and Merge for high-frequency commands such as drag or transform.",
			"ICommand",
			"command.merge.unsupported"};
	}
} // namespace DefectStudio
