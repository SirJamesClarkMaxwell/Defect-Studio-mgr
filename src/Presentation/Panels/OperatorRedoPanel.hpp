#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "Core/Diagnostics/StructuredError.hpp"
#include "Core/Undo/UndoStack.hpp"
#include "Presentation/Operators/SceneOperator.hpp"
#include "Renderer/Commands/SceneObjectsSnapshotCommand.hpp"

namespace DefectStudio
{
	// Blender's "adjust last operation" panel. It holds the scene as it was before the operator ran
	// and, on every parameter change, restores that snapshot and runs the operator again. The undo
	// stack sees exactly one entry no matter how many times a slider moves, which is why the
	// operator itself must not push one (see SceneOperator).
	class OperatorRedoPanel
	{
	public:
		// Captures the pre-operation scene, pushes ONE undo entry for the whole session, runs the
		// operator with its defaults and opens the panel. A failing run pushes nothing and leaves
		// the panel closed.
		[[nodiscard]] Result<void> RunAndOpen(const SceneOperator &op, RendererWindowState &window);

		// Restores the held snapshot AND the selection the operator consumed, then runs it again
		// with `values`. Does not touch the undo stack. A failing run closes the panel and leaves
		// the last good objects in place.
		[[nodiscard]] Result<void> Reapply(RendererWindowState &window, const SceneOperatorValues &values);

		// Closes the panel when the history moved under it, or the window is gone. Passing nullptr
		// for the window means it closed; nothing is restored into it.
		void PollInvalidation(const UndoStack &undoStack, const RendererWindowState *window) noexcept;

		void Close() noexcept;

		[[nodiscard]] bool IsOpen() const noexcept;

		// The values the panel last ran with - what its widgets display.
		[[nodiscard]] const SceneOperatorValues &Values() const noexcept;

		[[nodiscard]] const SceneOperator *CurrentOperator() const noexcept;

		// The ImGui body, drawn in the viewport's bottom-left corner. A no-op while closed.
		void Draw(RendererWindowState &window);

	private:
		void RefreshHiddenParameters(const RendererWindowState &window, const SceneOperatorValues &values);

		const SceneOperator *m_Operator = nullptr;
		std::optional<SceneObjectsSnapshot> m_Before;
		// An Add operation consumes the selection it ran on, so the snapshot alone is not enough to
		// re-run it: these are restored alongside it.
		std::vector<std::size_t> m_SelectedAtoms;
		std::vector<std::size_t> m_SelectedVacancies;
		SceneOperatorValues m_Values;
		std::vector<std::string> m_HiddenKeys;
		std::unordered_map<std::string, float> m_ParameterMaximums;
		std::string m_WindowId;
		std::size_t m_UndoDepth = 0;
		bool m_Open = false;
		bool m_Collapsed = false;
	};
}
