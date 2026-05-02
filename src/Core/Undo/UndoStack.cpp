#include "Core/dspch.hpp"

#include "Core/Undo/UndoStack.hpp"

namespace DefectStudio
{

	[[nodiscard]] StructuredError MakeUndoError(
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
			"UndoStack",
			std::move(code)};
	}
	

	UndoScope::UndoScope(UndoStack &stack, std::string description)
		: m_Stack(&stack), m_Active(true)
	{
		auto result = m_Stack->BeginGroup(std::move(description));
		if (!result)
			m_Active = false;
	}

	UndoScope::UndoScope(UndoScope &&other) noexcept
		: m_Stack(other.m_Stack), m_Active(other.m_Active)
	{
		other.m_Stack = nullptr;
		other.m_Active = false;
	}

	UndoScope::~UndoScope()
	{
		if (m_Stack != nullptr && m_Active)
			(void)m_Stack->EndGroup();
	}

	Result<void> UndoScope::Commit()
	{
		if (m_Stack == nullptr || !m_Active)
			return {};

		m_Active = false;
		return m_Stack->EndGroup();
	}

	Result<void> UndoScope::Cancel(CommandContext context)
	{
		if (m_Stack == nullptr || !m_Active)
			return {};

		m_Active = false;
		return m_Stack->CancelGroup(std::move(context));
	}

	bool UndoStack::PushExecuted(std::unique_ptr<ICommand> command)
	{
		if (!command || !command->IsUndoable() || m_IsApplying)
			return false;

		if (m_GroupDepth > 0)
		{
			if (!m_ActiveGroup)
				return false;
			m_ActiveGroup->commands.push_back(std::move(command));
			return true;
		}

		if (m_Index == m_History.size() && m_Index > 0)
		{
			UndoRecord &lastRecord = m_History[m_Index - 1];
			if (lastRecord.commands.size() == 1 && lastRecord.commands.front()->CanMerge(*command))
			{
				auto mergeResult = lastRecord.commands.front()->Merge(std::move(command));
				if (mergeResult)
				{
					invalidateCleanIfHistoryMutatedAtCleanPoint();
					return true;
				}
			}
		}

		UndoRecord record;
		record.description = command->Description();
		record.commands.push_back(std::move(command));
		return pushRecord(std::move(record)).HasValue();
	}

	Result<void> UndoStack::Undo(CommandContext context)
	{
		if (m_IsApplying)
		{
			return MakeUndoError(
				"undo.reentrant",
				"Undo was rejected.",
				"Undo was requested while undo/redo is already applying.",
				"Defer nested mutations through CommandRegistry.");
		}

		if (m_GroupDepth > 0)
		{
			return MakeUndoError(
				"undo.group_active",
				"Undo was rejected.",
				"Undo was requested while an undo group is still open.",
				"Commit or cancel the active group before undoing.");
		}

		if (!CanUndo())
			return {};

		m_IsApplying = true;
		Result<void> result = applyUndoRecord(m_History[m_Index - 1], context);
		m_IsApplying = false;

		if (!result)
			return result;

		--m_Index;
		return {};
	}

	Result<void> UndoStack::Redo(CommandContext context)
	{
		if (m_IsApplying)
		{
			return MakeUndoError(
				"redo.reentrant",
				"Redo was rejected.",
				"Redo was requested while undo/redo is already applying.",
				"Defer nested mutations through CommandRegistry.");
		}

		if (m_GroupDepth > 0)
		{
			return MakeUndoError(
				"redo.group_active",
				"Redo was rejected.",
				"Redo was requested while an undo group is still open.",
				"Commit or cancel the active group before redoing.");
		}

		if (!CanRedo())
			return {};

		m_IsApplying = true;
		Result<void> result = applyRedoRecord(m_History[m_Index], context);
		m_IsApplying = false;

		if (!result)
			return result;

		++m_Index;
		return {};
	}

	Result<void> UndoStack::BeginGroup(std::string description)
	{
		if (m_IsApplying)
		{
			return MakeUndoError(
				"undo.group.reentrant",
				"Undo group was rejected.",
				"BeginGroup was requested while undo/redo is applying.",
				"Only start undo groups from normal command execution.");
		}

		if (m_GroupDepth == 0)
		{
			m_ActiveGroup = UndoRecord{};
			m_ActiveGroup->description = std::move(description);
		}
		++m_GroupDepth;
		return {};
	}

	Result<void> UndoStack::EndGroup()
	{
		if (m_GroupDepth == 0 || !m_ActiveGroup)
		{
			return MakeUndoError(
				"undo.group.none",
				"Undo group cannot be closed.",
				"EndGroup was called without a matching BeginGroup.",
				"Use UndoScope or pair BeginGroup/EndGroup calls.");
		}

		--m_GroupDepth;
		if (m_GroupDepth > 0)
			return {};

		UndoRecord record = std::move(*m_ActiveGroup);
		m_ActiveGroup.reset();
		if (record.commands.empty())
			return {};

		if (record.description.empty())
			record.description = record.commands.front()->Description();
		return pushRecord(std::move(record));
	}

	Result<void> UndoStack::CancelGroup(CommandContext context)
	{
		if (m_GroupDepth == 0 || !m_ActiveGroup)
		{
			return MakeUndoError(
				"undo.group.none",
				"Undo group cannot be cancelled.",
				"CancelGroup was called without a matching BeginGroup.",
				"Use UndoScope or pair BeginGroup/CancelGroup calls.");
		}

		UndoRecord record = std::move(*m_ActiveGroup);
		m_ActiveGroup.reset();
		m_GroupDepth = 0;

		m_IsApplying = true;
		Result<void> result = applyUndoRecord(record, context);
		m_IsApplying = false;
		return result;
	}

	UndoScope UndoStack::ScopedGroup(std::string description)
	{
		return UndoScope(*this, std::move(description));
	}

	void UndoStack::Clear()
	{
		m_History.clear();
		m_Index = 0;
		m_CleanIndex = 0;
		m_ActiveGroup.reset();
		m_GroupDepth = 0;
		m_IsApplying = false;
	}

	void UndoStack::MarkClean()
	{
		m_CleanIndex = m_Index;
	}

	bool UndoStack::IsClean() const noexcept
	{
		return m_CleanIndex.has_value() && *m_CleanIndex == m_Index;
	}

	bool UndoStack::CanUndo() const noexcept
	{
		return m_Index > 0;
	}

	bool UndoStack::CanRedo() const noexcept
	{
		return m_Index < m_History.size();
	}

	bool UndoStack::IsApplying() const noexcept
	{
		return m_IsApplying;
	}

	bool UndoStack::HasActiveGroup() const noexcept
	{
		return m_GroupDepth > 0;
	}

	std::size_t UndoStack::GetUndoDepth() const noexcept
	{
		return m_Index;
	}

	std::size_t UndoStack::GetRedoDepth() const noexcept
	{
		return m_History.size() - m_Index;
	}

	std::string UndoStack::GetUndoDescription() const
	{
		if (!CanUndo())
			return {};
		return m_History[m_Index - 1].description;
	}

	std::string UndoStack::GetRedoDescription() const
	{
		if (!CanRedo())
			return {};
		return m_History[m_Index].description;
	}

	Result<void> UndoStack::pushRecord(UndoRecord record)
	{
		if (record.commands.empty())
			return {};

		dropRedoBranch();
		m_History.push_back(std::move(record));
		m_Index = m_History.size();
		invalidateCleanIfHistoryMutatedAtCleanPoint();
		return {};
	}

	Result<void> UndoStack::applyUndoRecord(UndoRecord &record, CommandContext &context)
	{
		for (auto it = record.commands.rbegin(); it != record.commands.rend(); ++it)
		{
			Result<void> result = (*it)->Undo(context);
			if (!result)
				return result;
		}
		return {};
	}

	Result<void> UndoStack::applyRedoRecord(UndoRecord &record, CommandContext &context)
	{
		for (auto &command : record.commands)
		{
			Result<void> result = command->Execute(context);
			if (!result)
				return result;
		}
		return {};
	}

	void UndoStack::dropRedoBranch()
	{
		if (m_Index < m_History.size())
			m_History.erase(m_History.begin() + static_cast<std::ptrdiff_t>(m_Index), m_History.end());
	}

	void UndoStack::invalidateCleanIfHistoryMutatedAtCleanPoint()
	{
		if (m_CleanIndex.has_value() && *m_CleanIndex >= m_Index)
			m_CleanIndex.reset();
	}
} // namespace DefectStudio
