#pragma once

#include "Core/Commands/Command.hpp"
#include "Core/Utils/Memory.hpp"

namespace DefectStudio
{
	class EventBus;
	class UndoStack;

	class UndoCommand final : public ICommand
	{
	public:
		explicit UndoCommand(Ref<UndoStack> undoStack);
		Result<void> Execute(CommandContext &context) override;
		std::string Description() const override { return "Undo"; }

	private:
		Ref<UndoStack> m_UndoStack;
	};

	class RedoCommand final : public ICommand
	{
	public:
		explicit RedoCommand(Ref<UndoStack> undoStack);
		Result<void> Execute(CommandContext &context) override;
		std::string Description() const override { return "Redo"; }

	private:
		Ref<UndoStack> m_UndoStack;
	};

	class OpenCommandPaletteCommand final : public ICommand
	{
	public:
		explicit OpenCommandPaletteCommand(Ref<EventBus> eventBus);
		Result<void> Execute(CommandContext &context) override;
		std::string Description() const override { return "Open Command Palette"; }

	private:
		Ref<EventBus> m_EventBus;
	};

	class QuitCommand final : public ICommand
	{
	public:
		explicit QuitCommand(Ref<EventBus> eventBus);
		Result<void> Execute(CommandContext &context) override;
		std::string Description() const override { return "Quit Application"; }

	private:
		Ref<EventBus> m_EventBus;
	};

	class SaveProjectCommand final : public ICommand
	{
	public:
		explicit SaveProjectCommand(Ref<EventBus> eventBus);
		Result<void> Execute(CommandContext &context) override;
		std::string Description() const override { return "Save Project"; }

	private:
		Ref<EventBus> m_EventBus;
	};
} // namespace DefectStudio
