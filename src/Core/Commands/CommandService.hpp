#pragma once

#include <cstddef>
#include <functional>
#include <unordered_map>

#include "Core/Commands/Command.hpp"
#include "Core/Commands/CommandContext.hpp"
#include "Core/Commands/CommandRegistry.hpp"
#include "Core/Diagnostics/StructuredError.hpp"
#include "Core/Utils/Memory.hpp"

namespace DefectStudio
{
	class UndoStack;
	class CapabilityService;

	class CommandService
	{
	public:
		CommandService(
			Ref<CommandRegistry> registry,
			WeakRef<UndoStack> undoStack,
			WeakRef<CapabilityService> capabilityService = {});

		CommandService(const CommandService &) = delete;
		CommandService &operator=(const CommandService &) = delete;
		CommandService(CommandService &&) = delete;
		CommandService &operator=(CommandService &&) = delete;

		// Główny punkt wejścia – jedyna ścieżka wykonania komendy
		[[nodiscard]] Result<CommandOutcome> Execute(const CommandID &id, CommandContext context = {});

		// Undo/Redo przechodzi przez ten sam serwis
		[[nodiscard]] Result<void> Undo(CommandContext context = {});
		[[nodiscard]] Result<void> Redo(CommandContext context = {});

		// Zapytania – delegowane do Registry
		[[nodiscard]] Result<void> CanExecute(const CommandID &id) const;
		[[nodiscard]] Result<CommandMeta> GetMeta(const CommandID &id) const;
		[[nodiscard]] bool HasCommand(const CommandID &id) const;

		// Obserwatorzy wykonania (UI, logi, testy)
		[[nodiscard]] std::size_t AddObserver(CommandExecutionObserver observer);
		void RemoveObserver(std::size_t observerId);

	private:
		void notifyObservers(const CommandExecutionEvent &event) const;

		Ref<CommandRegistry> m_Registry;
		WeakRef<UndoStack> m_UndoStack;
		WeakRef<CapabilityService> m_CapabilityService;

		std::unordered_map<std::size_t, CommandExecutionObserver> m_Observers;
		std::size_t m_NextObserverId = 1;
		bool m_IsExecuting = false;
	};
} // namespace DefectStudio
