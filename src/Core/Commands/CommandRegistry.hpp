#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "Core/Capabilities/CapabilityService.hpp"
#include "Core/Commands/Command.hpp"
#include "Core/Diagnostics/StructuredError.hpp"

namespace DefectStudio
{
	class UndoStack;

	enum class CommandExecutionState
	{
		Started,
		Succeeded,
		Failed,
		Rejected
	};

	struct CommandExecutionEvent
	{
		CommandID id;
		std::string name;
		CommandExecutionState state = CommandExecutionState::Started;
		std::optional<StructuredError> error;
	};

	using CommandFactory = std::function<std::unique_ptr<ICommand>(CommandContext &)>;
	using CommandExecutionObserver = std::function<void(const CommandExecutionEvent &)>;

	class CommandRegistry
	{
	public:
		explicit CommandRegistry(CapabilityService *capabilityService = nullptr);

		void SetCapabilityService(CapabilityService *capabilityService) noexcept;
		void SetUndoStack(UndoStack *undoStack) noexcept;

		[[nodiscard]] Result<void> Register(CommandMeta meta, CommandFactory factory);
		[[nodiscard]] bool HasCommand(const CommandID &id) const;
		[[nodiscard]] Result<CommandMeta> GetMeta(const CommandID &id) const;
		[[nodiscard]] std::vector<CommandMeta> ListCommands() const;
		[[nodiscard]] Result<void> CanExecute(const CommandID &id) const;

		[[nodiscard]] Result<CommandOutcome> Execute(const CommandID &id, CommandContext context = {});

		[[nodiscard]] std::size_t AddObserver(CommandExecutionObserver observer);
		void RemoveObserver(std::size_t observerId);

	private:
		struct RegisteredCommand
		{
			CommandMeta meta;
			CommandFactory factory;
		};

		[[nodiscard]] Result<void> ValidateCapabilities(const CommandMeta &meta) const;
		void Notify(const CommandExecutionEvent &event) const;

		CapabilityService *m_CapabilityService = nullptr;
		UndoStack *m_UndoStack = nullptr;
		std::unordered_map<std::string, RegisteredCommand> m_Commands;
		std::unordered_map<std::size_t, CommandExecutionObserver> m_Observers;
		std::size_t m_NextObserverId = 1;
		bool m_IsExecuting = false;
	};
} // namespace DefectStudio
