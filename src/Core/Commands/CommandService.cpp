#include "Core/dspch.hpp"

#include "Core/Commands/CommandService.hpp"

#include "Core/Capabilities/CapabilityService.hpp"
#include "Core/Undo/UndoStack.hpp"
#include "Core/Utils/Assert.hpp"
#include "Core/Utils/Logger.hpp"

namespace DefectStudio
{
	[[nodiscard]] static StructuredError makeServiceError(
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
			"CommandService",
			std::move(code)};
	}

	CommandService::CommandService(
		Ref<CommandRegistry> registry,
		WeakRef<UndoStack> undoStack,
		WeakRef<CapabilityService> capabilityService)
		: m_Registry(std::move(registry))
		, m_UndoStack(std::move(undoStack))
		, m_CapabilityService(std::move(capabilityService))
	{
		DS_ASSERT(m_Registry != nullptr, "CommandService requires a valid CommandRegistry");
	}

	Result<CommandOutcome> CommandService::Execute(const CommandID &id, CommandContext context)
	{
		if (m_IsExecuting)
		{
			return makeServiceError(
				"command.execute.reentrant",
				"Command execution was rejected.",
				"Command '" + id.value + "' attempted to execute while another command is active.",
				"Defer nested work through the job system or main-thread dispatcher.");
		}

		auto metaResult = m_Registry->GetMeta(id);
		if (!metaResult)
			return metaResult.Error();

		if (!metaResult->requiredCapabilities.empty())
		{
			auto capService = m_CapabilityService.lock();
			if (capService == nullptr)
			{
				return makeServiceError(
					"command.capability.no_service",
					"Command cannot verify required capabilities.",
					"Command '" + id.value + "' declares capability requirements but no CapabilityService is available.",
					"Attach CapabilityService to CommandService during application startup.",
					ErrorCategory::Capability);
			}

			for (const CapabilityID &cap : metaResult->requiredCapabilities)
			{
				auto requireResult = capService->Require(cap.value);
				if (!requireResult)
				{
					notifyObservers({id, metaResult->name, CommandExecutionState::Rejected, requireResult.Error()});
					return requireResult.Error();
				}
			}
		}

		m_IsExecuting = true;
		notifyObservers({id, metaResult->name, CommandExecutionState::Started, std::nullopt});

		auto result = m_Registry->Execute(id, std::move(context));

		m_IsExecuting = false;

		if (!result)
		{
			notifyObservers({id, metaResult->name, CommandExecutionState::Failed, result.Error()});
			return result.Error();
		}

		notifyObservers({id, metaResult->name, CommandExecutionState::Succeeded, std::nullopt});
		return result;
	}

	Result<void> CommandService::Undo(CommandContext context)
	{
		auto undoStack = m_UndoStack.lock();
		if (undoStack == nullptr)
		{
			return makeServiceError(
				"command.undo.no_stack",
				"Undo is not available.",
				"CommandService has no UndoStack attached.",
				"Attach an UndoStack to CommandService during application startup.");
		}
		return undoStack->Undo(std::move(context));
	}

	Result<void> CommandService::Redo(CommandContext context)
	{
		auto undoStack = m_UndoStack.lock();
		if (undoStack == nullptr)
		{
			return makeServiceError(
				"command.redo.no_stack",
				"Redo is not available.",
				"CommandService has no UndoStack attached.",
				"Attach an UndoStack to CommandService during application startup.");
		}
		return undoStack->Redo(std::move(context));
	}

	Result<void> CommandService::CanExecute(const CommandID &id) const
	{
		return m_Registry->CanExecute(id);
	}

	Result<CommandMeta> CommandService::GetMeta(const CommandID &id) const
	{
		return m_Registry->GetMeta(id);
	}

	bool CommandService::HasCommand(const CommandID &id) const
	{
		return m_Registry->HasCommand(id);
	}

	std::size_t CommandService::AddObserver(CommandExecutionObserver observer)
	{
		if (!observer)
			return 0;
		const std::size_t observerId = m_NextObserverId++;
		m_Observers.emplace(observerId, std::move(observer));
		return observerId;
	}

	void CommandService::RemoveObserver(std::size_t observerId)
	{
		m_Observers.erase(observerId);
	}

	void CommandService::notifyObservers(const CommandExecutionEvent &event) const
	{
		for (const auto &[id, observer] : m_Observers)
		{
			(void)id;
			if (observer)
				observer(event);
		}
	}
} // namespace DefectStudio
