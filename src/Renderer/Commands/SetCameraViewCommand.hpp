#pragma once

#include "Core/Commands/Command.hpp"
#include "Renderer/RendererTypes.hpp"

namespace DefectStudio
{
	// Undoable: zmiana widoku kamery (orbit, pan, zoom, transition).
	// Execute: przechodzi do newView. Undo: wraca do oldView.
	// CanMerge: scala kolejne małe zmiany (np. ciągły orbit myszką) w jeden rekord.
	class SetCameraViewCommand final : public ICommand
	{
	public:
		SetCameraViewCommand(
			RendererViewSnapshot oldView,
			RendererViewSnapshot newView,
			std::string description,
			std::string windowId);

		[[nodiscard]] Result<void> Execute(CommandContext &context) override;
		[[nodiscard]] Result<void> Undo(CommandContext &context) override;
		[[nodiscard]] Result<void> Redo(CommandContext &context) override;
		[[nodiscard]] std::string Description() const override;
		[[nodiscard]] bool IsUndoable() const noexcept override;
		[[nodiscard]] bool CanMerge(const ICommand &next) const noexcept override;
		[[nodiscard]] Result<void> Merge(std::unique_ptr<ICommand> next) override;

		[[nodiscard]] const std::string &WindowId() const noexcept;

	private:
		RendererViewSnapshot m_OldView;
		RendererViewSnapshot m_NewView;
		std::string m_Description;
		std::string m_WindowId;
	};
} // namespace DefectStudio
