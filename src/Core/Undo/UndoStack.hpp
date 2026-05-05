#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "Core/Commands/Command.hpp"
#include "Core/Commands/CommandContext.hpp"
#include "Core/Diagnostics/StructuredError.hpp"

namespace DefectStudio
{
	class UndoStack;

	class UndoScope
	{
	public:
		UndoScope(UndoStack &stack, std::string description);
		UndoScope(const UndoScope &) = delete;
		UndoScope &operator=(const UndoScope &) = delete;
		UndoScope(UndoScope &&other) noexcept;
		UndoScope &operator=(UndoScope &&other) noexcept = delete;
		~UndoScope();

		[[nodiscard]] Result<void> Commit();
		[[nodiscard]] Result<void> Cancel(CommandContext context = {});

	private:
		UndoStack *m_Stack = nullptr;
		bool m_Active = false;
	};
	struct UndoRecord
	{
		std::string description;
		std::vector<std::unique_ptr<ICommand>> commands;
	};

	class UndoStack
	{
	public:
		[[nodiscard]] bool PushExecuted(std::unique_ptr<ICommand> command);

		[[nodiscard]] Result<void> Undo(CommandContext context = {});
		[[nodiscard]] Result<void> Redo(CommandContext context = {});

		[[nodiscard]] Result<void> BeginGroup(std::string description);
		[[nodiscard]] Result<void> EndGroup();
		[[nodiscard]] Result<void> CancelGroup(CommandContext context = {});
		[[nodiscard]] UndoScope ScopedGroup(std::string description);

		void Clear();
		void MarkClean();

		[[nodiscard]] bool IsClean() const noexcept;
		[[nodiscard]] bool CanUndo() const noexcept;
		[[nodiscard]] bool CanRedo() const noexcept;
		[[nodiscard]] bool IsApplying() const noexcept;
		[[nodiscard]] bool HasActiveGroup() const noexcept;
		[[nodiscard]] std::size_t GetUndoDepth() const noexcept;
		[[nodiscard]] std::size_t GetRedoDepth() const noexcept;
		[[nodiscard]] std::string GetUndoDescription() const;
		[[nodiscard]] std::string GetRedoDescription() const;

	private:
		[[nodiscard]] Result<void> pushRecord(UndoRecord record);
		[[nodiscard]] Result<void> applyUndoRecord(UndoRecord &record, CommandContext &context);
		[[nodiscard]] Result<void> applyRedoRecord(UndoRecord &record, CommandContext &context);
		void dropRedoBranch();
		void invalidateCleanIfHistoryMutatedAtCleanPoint();

		std::vector<UndoRecord> m_History;
		std::size_t m_Index = 0;
		std::optional<std::size_t> m_CleanIndex = 0;

		std::optional<UndoRecord> m_ActiveGroup;
		std::size_t m_GroupDepth = 0;
		bool m_GroupCancelled = false;
		bool m_IsApplying = false;
	};
} // namespace DefectStudio
