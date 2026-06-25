#include "Core/dspch.hpp"

#include "Renderer/Commands/SetCameraViewCommand.hpp"

namespace DefectStudio
{
	SetCameraViewCommand::SetCameraViewCommand(
		RendererViewSnapshot oldView,
		RendererViewSnapshot newView,
		std::string description,
		std::string windowId)
		: m_OldView(std::move(oldView)),
		  m_NewView(std::move(newView)),
		  m_Description(std::move(description)),
		  m_WindowId(std::move(windowId))
	{
	}

	Result<void> SetCameraViewCommand::Execute(CommandContext &)
	{
		return {};
	}

	Result<void> SetCameraViewCommand::Undo(CommandContext &)
	{
		return {};
	}

	Result<void> SetCameraViewCommand::Redo(CommandContext &context)
	{
		return Execute(context);
	}

	std::string SetCameraViewCommand::Description() const
	{
		return m_Description;
	}

	bool SetCameraViewCommand::IsUndoable() const noexcept
	{
		return true;
	}

	bool SetCameraViewCommand::CanMerge(const ICommand &next) const noexcept
	{
		const auto *other = dynamic_cast<const SetCameraViewCommand *>(&next);
		return other != nullptr && other->m_WindowId == m_WindowId;
	}

	Result<void> SetCameraViewCommand::Merge(std::unique_ptr<ICommand> next)
	{
		auto *other = dynamic_cast<SetCameraViewCommand *>(next.get());
		if (other == nullptr)
		{
			return StructuredError{
				ErrorCategory::Validation,
				Severity::Error,
				"Cannot merge incompatible commands.",
				"SetCameraViewCommand::Merge received a command with a different runtime type.",
				"Merge only SetCameraViewCommand instances for the same renderer window.",
				"SetCameraViewCommand",
				"renderer.camera.merge_type_mismatch"};
		}

		m_NewView = other->m_NewView;
		m_Description = other->m_Description;
		return {};
	}

	const std::string &SetCameraViewCommand::WindowId() const noexcept
	{
		return m_WindowId;
	}
} // namespace DefectStudio
