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
	// InputText - Ctrl+Enter or Shift+Enter submits the whole cell's lines to IPython in order and
	// clears it, Enter alone just inserts a newline like any code editor. Plain Up/Down recall
	// history the way a shell does: only while the cell is empty (nothing to conflict with -
	// there's no cursor-navigation or autocomplete-popup meaning to steal) or while already
	// mid-recall (any manual edit cancels that and hands Up/Down back to the editor).
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
		// Plain Up/Down history recall - see class comment for when it applies.
		void handleHistoryNavigation();
		// Cheap heuristic scan for `name = ...`/`def name`/`class name` in submitted code, so
		// session variables/functions become autocomplete-able - not a real parser, see .cpp.
		void recordIdentifiersFrom(const std::string &code);
		void addKnownIdentifier(std::string identifier);
		// Static list only - keywords/builtins/known identifiers. A live-IPython-backed variant
		// (real `obj.attr` completion) was tried and reverted: it flooded the session with one
		// In[]/Out[] per render frame the popup stayed open, see git history on this file if
		// revisiting.
		void provideSuggestions(TextEditor::AutoCompleteState &state);
		void pollProcessOutput();

		Platform::InteractiveProcess m_Process;
		std::vector<Segment> m_Segments;
		std::string m_StartError;
		bool m_StartAttempted = false;
		// True once IPython has printed anything at all (its first prompt, at minimum) - distinct
		// from `!m_Segments.empty()`, which the Clear button resets to true even though the
		// running session is still perfectly ready for more input.
		bool m_ReceivedFirstOutput = false;

		std::string m_ProjectRoot;
		std::vector<std::string> m_ProjectRoots;
		std::vector<std::string> m_KnownIdentifiers;

		std::vector<std::string> m_History;
		// -1 = not browsing history (editing normally). Otherwise an index into m_History - Up
		// moves toward 0 (older), Down moves back toward m_History.size() (newer, then off the end
		// restores m_HistoryDraft).
		int m_HistoryCursor = -1;
		std::string m_HistoryDraft;
		// Exactly what we last wrote into the editor via history recall - if the buffer no longer
		// matches this, the user edited it manually, which cancels browsing (see
		// handleHistoryNavigation).
		std::string m_LastRecalledText;

		TextEditor m_InputEditor;
		// Must outlive every call the editor makes into it - stored as a member, not a local,
		// since SetAutoCompleteConfig takes a raw pointer.
		TextEditor::AutoCompleteConfig m_AutoCompleteConfig;
	};
} // namespace DefectStudio
