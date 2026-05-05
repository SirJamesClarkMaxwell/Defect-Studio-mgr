#include "Core/dspch.hpp"

#include "Core/Commands/SystemCommands.hpp"

#include "App/Events/ApplicationEvents.hpp"
#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Core/Undo/UndoStack.hpp"
#include "Core/Utils/Assert.hpp"

namespace DefectStudio
{
	UndoCommand::UndoCommand(Ref<UndoStack> undoStack)
		: m_UndoStack(std::move(undoStack))
	{
		DS_ASSERT(m_UndoStack != nullptr, "UndoCommand requires an UndoStack");
	}

	Result<void> UndoCommand::Execute(CommandContext &context)
	{
		return m_UndoStack->Undo(std::move(context));
	}

	RedoCommand::RedoCommand(Ref<UndoStack> undoStack)
		: m_UndoStack(std::move(undoStack))
	{
		DS_ASSERT(m_UndoStack != nullptr, "RedoCommand requires an UndoStack");
	}

	Result<void> RedoCommand::Execute(CommandContext &context)
	{
		return m_UndoStack->Redo(std::move(context));
	}

	OpenCommandPaletteCommand::OpenCommandPaletteCommand(Ref<EventBus> eventBus)
		: m_EventBus(std::move(eventBus))
	{
		DS_ASSERT(m_EventBus != nullptr, "OpenCommandPaletteCommand requires EventBus");
	}

	Result<void> OpenCommandPaletteCommand::Execute(CommandContext &)
	{
		m_EventBus->Queue(AppEvents::OpenCommandPaletteRequested{});
		return Result<void>{};
	}

	QuitCommand::QuitCommand(Ref<EventBus> eventBus)
		: m_EventBus(std::move(eventBus))
	{
		DS_ASSERT(m_EventBus != nullptr, "QuitCommand requires EventBus");
	}

	Result<void> QuitCommand::Execute(CommandContext &)
	{
		m_EventBus->Queue(AppEvents::ShutdownRequested{});
		return Result<void>{};
	}

	SaveProjectCommand::SaveProjectCommand(Ref<EventBus> eventBus)
		: m_EventBus(std::move(eventBus))
	{
		DS_ASSERT(m_EventBus != nullptr, "SaveProjectCommand requires EventBus");
	}

	Result<void> SaveProjectCommand::Execute(CommandContext &)
	{
		m_EventBus->Queue(AppEvents::ProjectSaveRequested{});
		return Result<void>{};
	}
} // namespace DefectStudio
