#pragma once

#include <string>
#include <vector>

#include <TextEditor.h>
#include <imgui.h>

#include "Core/Platform/InteractiveProcess.hpp"
#include "Presentation/Panels/IPanel.hpp"

namespace DefectStudio
{
	// Dockable IPython-backed console - deliberately NOT a terminal (see TerminalPanel for that):
	// plain InteractiveProcess pipes, no VT100/ConPTY, no ANSI rendering. IPython's
	// `--simple-prompt` mode exists precisely for this "driven over a plain pipe" case - it prints
	// bare `In [1]:`/`Out[1]:` text and still accumulates multi-line blocks (if/for/def) itself
	// before executing, so this panel never needs to parse Python syntax. Always the app's own
	// `.venv` (never system Python) - see
	// docs/work/project/plans/2026-08-24-calc-tools.md section 4.
	//
	// Input is a real code cell (ImGuiColorTextEdit, same component TextEditorPanel uses, with its
	// built-in Python language definition for syntax highlighting) rather than a single-line
	// InputText - Ctrl+Enter submits the whole cell's lines to IPython in order and clears it,
	// Enter alone just inserts a newline like any code editor. This drops the earlier plain-
	// InputText's Up/Down history recall (doesn't translate to a multi-line editor - Up/Down there
	// means "move the cursor").
	class CalculatorConsolePanel final : public IPanel
	{
	public:
		explicit CalculatorConsolePanel(std::string title = "Integrated Python Console", bool visibleByDefault = false);

		void Render() override;
		[[nodiscard]] Ref<IPanel> Clone() const override;

		// Snapshot pushed by EditorLayer whenever the active project/roots change (see
		// EditorLayer::refreshProjectDependentPanels) - injected as plain `project_root`/
		// `project_roots` Python variables the first time the console starts. One-way, one-time:
		// no live IPC/binding back into the running app (deliberately out of scope, see
		// docs/work/project/plans/2026-08-24-calc-tools.md section 4) - if the project changes
		// after the console session is already running, restart the panel to pick up the new
		// values.
		void SetProjectContext(std::string projectRoot, std::vector<std::string> projectRoots);

	private:
		struct Segment
		{
			std::string text;
			bool isUserInput = false;
		};

		void ensureStarted();
		void appendSegment(std::string text, bool isUserInput);
		void submitCurrentCell();

		Platform::InteractiveProcess m_Process;
		std::vector<Segment> m_Segments;
		std::string m_StartError;
		bool m_StartAttempted = false;

		std::string m_ProjectRoot;
		std::vector<std::string> m_ProjectRoots;

		TextEditor m_InputEditor;
	};
} // namespace DefectStudio
