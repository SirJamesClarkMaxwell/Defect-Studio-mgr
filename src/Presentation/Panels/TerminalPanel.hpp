#pragma once

#include <string>
#include <vector>

#include <imgui.h>

#include "Core/Platform/ConPtyProcess.hpp"
#include "Core/Utils/Memory.hpp"
#include "Presentation/Panels/IPanel.hpp"
#include "Presentation/Terminal/VtScreen.hpp"

namespace DefectStudio
{
	// Dockable, ConPTY-backed terminal - real inline line editing and Tab completion (see
	// VtScreen.hpp for exactly what "real" covers and what it doesn't in this MVP). General-
	// purpose shell (e.g. `ssh user@host` to reach a server), not aware of "servers" as a
	// concept at this layer. Tabs each hold one or more side-by-side panes (VSCode-style split
	// terminal) - one ConPtyProcess + VtScreen per pane, so several server connections can stay
	// open at once, split or in separate tabs. See
	// docs/work/project/plans/2026-08-24-calc-tools.md section 3.
	class TerminalPanel final : public IPanel
	{
	public:
		explicit TerminalPanel(std::string title = "Terminal", bool visibleByDefault = false);

		void Render() override;
		[[nodiscard]] Ref<IPanel> Clone() const override;

		// Entry points for keybound commands (EditorLayer) - see keybindings.yaml
		// terminal.focus/new_tab/close_current/split.
		void RequestFocus();
		void OpenNewTab();
		void CloseActiveTab();
		void SplitActivePane();

	private:
		struct Session
		{
			Platform::ConPtyProcess process;
			VtScreen screen{80, 24};
			std::string startError;
			bool startAttempted = false;
			int columns = 80;
			int rows = 24;
			int id = 0;
		};

		struct Tab
		{
			std::vector<Unique<Session>> panes;
			int focusedPaneIndex = 0;
			int id = 0;
			std::string label;
			bool wantsSelect = false;
		};

		static void ensureStarted(Session &session);
		static void forwardKeyboardInput(Session &session);
		// Returns whether this pane has keyboard focus this frame.
		bool renderSession(Session &session, float width, bool focusRequested);
		void renderTab(Tab &tab);

		std::vector<Unique<Tab>> m_Tabs;
		int m_NextTabId = 1;
		int m_NextSessionId = 1;
		int m_ActiveTabIndex = 0;
		bool m_FocusRequested = false;
	};
} // namespace DefectStudio
