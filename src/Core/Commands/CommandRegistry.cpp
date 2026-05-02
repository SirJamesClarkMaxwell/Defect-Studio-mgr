#include "Core/dspch.hpp"

#include "Core/Commands/CommandRegistry.hpp"

#include "Core/Undo/UndoStack.hpp"

namespace DefectStudio
{

	[[nodiscard]] StructuredError MakeCommandError(
		std::string code,
		std::string userMessage,
		std::string technicalDetails,
		std::string suggestion,
		ErrorCategory category = ErrorCategory::Validation)
	{
		return StructuredError{
			category,
			Severity::Error,
			std::move(userMessage),
			std::move(technicalDetails),
			std::move(suggestion),
			"CommandRegistry",
			std::move(code)};
	}


	CommandRegistry::CommandRegistry(CapabilityService *capabilityService)
		: m_CapabilityService(capabilityService)
	{
	}

	void CommandRegistry::SetCapabilityService(CapabilityService *capabilityService) noexcept
	{
		m_CapabilityService = capabilityService;
	}

	void CommandRegistry::SetUndoStack(UndoStack *undoStack) noexcept
	{
		m_UndoStack = undoStack;
	}

	Result<void> CommandRegistry::Register(CommandMeta meta, CommandFactory factory)
	{
		if (!meta.id)
		{
			return MakeCommandError(
				"command.register.empty_id",
				"Command registration failed.",
				"CommandMeta.id is empty.",
				"Provide a stable command id such as 'project.open' or 'edit.undo'.");
		}

		if (!factory)
		{
			return MakeCommandError(
				"command.register.missing_factory",
				"Command registration failed.",
				"Factory is empty for command '" + meta.id.value + "'.",
				"Register commands with a factory that creates a fresh ICommand instance per execution.");
		}

		const std::string commandId = meta.id.value;
		const auto [it, inserted] = m_Commands.emplace(commandId, RegisteredCommand{std::move(meta), std::move(factory)});
		if (!inserted)
		{
			return MakeCommandError(
				"command.register.duplicate_id",
				"Command registration failed.",
				"Command id '" + it->first + "' is already registered.",
				"Use globally unique command ids and keep aliases outside CommandRegistry.");
		}

		return {};
	}

	bool CommandRegistry::HasCommand(const CommandID &id) const
	{
		return m_Commands.find(id.value) != m_Commands.end();
	}

	Result<CommandMeta> CommandRegistry::GetMeta(const CommandID &id) const
	{
		auto it = m_Commands.find(id.value);
		if (it == m_Commands.end())
		{
			return MakeCommandError(
				"command.lookup.not_found",
				"Command was not found.",
				"Command id '" + id.value + "' is not registered.",
				"Register the command before binding it to UI, hotkeys or scripts.");
		}
		return it->second.meta;
	}

	std::vector<CommandMeta> CommandRegistry::ListCommands() const
	{
		std::vector<CommandMeta> commands;
		commands.reserve(m_Commands.size());
		for (const auto &[id, command] : m_Commands)
		{
			(void)id;
			commands.push_back(command.meta);
		}
		return commands;
	}

	Result<void> CommandRegistry::CanExecute(const CommandID &id) const
	{
		auto it = m_Commands.find(id.value);
		if (it == m_Commands.end())
		{
			return MakeCommandError(
				"command.execute.not_found",
				"Command was not found.",
				"Command id '" + id.value + "' is not registered.",
				"Register the command before executing it.");
		}
		return ValidateCapabilities(it->second.meta);
	}

	Result<CommandOutcome> CommandRegistry::Execute(const CommandID &id, CommandContext context)
	{
		if (m_IsExecuting)
		{
			return MakeCommandError(
				"command.execute.reentrant",
				"Command execution was rejected.",
				"Command '" + id.value + "' attempted to execute while another command is active.",
				"Defer nested work through the command runtime or main-thread dispatcher.");
		}

		auto it = m_Commands.find(id.value);
		if (it == m_Commands.end())
		{
			return MakeCommandError(
				"command.execute.not_found",
				"Command was not found.",
				"Command id '" + id.value + "' is not registered.",
				"Register the command before executing it.");
		}

		const RegisteredCommand &registration = it->second;
		Notify(CommandExecutionEvent{id, registration.meta.name, CommandExecutionState::Started, std::nullopt});

		auto capabilityResult = ValidateCapabilities(registration.meta);
		if (!capabilityResult)
		{
			Notify(CommandExecutionEvent{id, registration.meta.name, CommandExecutionState::Rejected, capabilityResult.Error()});
			return capabilityResult.Error();
		}

		ExecutionScope	scope(m_IsExecuting);

		std::unique_ptr<ICommand> command = registration.factory(context);
		if (!command)
		{
			auto error = MakeCommandError(
				"command.execute.factory_null",
				"Command execution failed.",
				"Factory returned null for command '" + id.value + "'.",
				"Command factories must return a valid ICommand instance.");
			Notify(CommandExecutionEvent{id, registration.meta.name, CommandExecutionState::Failed, error});
			return error;
		}

		Result<void> executionResult = command->Execute(context);
		if (!executionResult)
		{
			Notify(CommandExecutionEvent{id, registration.meta.name, CommandExecutionState::Failed, executionResult.Error()});
			return executionResult.Error();
		}

		CommandOutcome outcome;
		outcome.id = id;
		outcome.description = command->Description();
		outcome.undoable = command->IsUndoable();

		if (outcome.undoable && m_UndoStack != nullptr)
			outcome.pushedToUndoStack = m_UndoStack->PushExecuted(std::move(command));

		Notify(CommandExecutionEvent{id, registration.meta.name, CommandExecutionState::Succeeded, std::nullopt});
		return outcome;
	}

	std::size_t CommandRegistry::AddObserver(CommandExecutionObserver observer)
	{
		if (!observer)
			return 0;

		const std::size_t observerId = m_NextObserverId++;
		m_Observers.emplace(observerId, std::move(observer));
		return observerId;
	}

	void CommandRegistry::RemoveObserver(std::size_t observerId)
	{
		m_Observers.erase(observerId);
	}

	Result<void> CommandRegistry::ValidateCapabilities(const CommandMeta &meta) const
	{
		if (meta.requiredCapabilities.empty())
			return {};

		if (m_CapabilityService == nullptr)
		{
			return MakeCommandError(
				"command.capability.no_service",
				"Command cannot verify required capabilities.",
				"Command '" + meta.id.value + "' declares capability requirements but no CapabilityService is attached.",
				"Attach CapabilityService to CommandRegistry during application startup.",
				ErrorCategory::Capability);
		}

		for (const CapabilityID &capability : meta.requiredCapabilities)
		{
			if (!m_CapabilityService->IsAvailable(capability.value))
			{
				return MakeCommandError(
					"command.capability.unavailable",
					"Command is not available in the current runtime.",
					"Required capability '" + capability.value + "' is unavailable for command '" + meta.id.value + "'.",
					"Check capability diagnostics or disable the feature behind this command.",
					ErrorCategory::Capability);
			}
		}

		return {};
	}

	void CommandRegistry::Notify(const CommandExecutionEvent &event) const
	{
		for (const auto &[id, observer] : m_Observers)
		{
			(void)id;
			observer(event);
		}
	}
} // namespace DefectStudio
