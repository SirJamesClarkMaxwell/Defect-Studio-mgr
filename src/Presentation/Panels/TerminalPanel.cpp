#include "Core/dspch.hpp"

#include "Presentation/Panels/TerminalPanel.hpp"

#include <algorithm>
#include <cmath>

#include <imgui_internal.h> // ImTextCharToUtf8 - same low-level include RendererPanel.cpp uses

#include "Presentation/EditorFonts.hpp"

namespace DefectStudio
{
	namespace
	{
		constexpr ImU32 kAnsiPalette[16] = {
			IM_COL32(0x00, 0x00, 0x00, 255), IM_COL32(0xCD, 0x31, 0x31, 255), IM_COL32(0x0D, 0xBC, 0x79, 255),
			IM_COL32(0xE5, 0xE5, 0x10, 255), IM_COL32(0x24, 0x72, 0xC8, 255), IM_COL32(0xBC, 0x3F, 0xBC, 255),
			IM_COL32(0x11, 0xA8, 0xCD, 255), IM_COL32(0xE5, 0xE5, 0xE5, 255), IM_COL32(0x66, 0x66, 0x66, 255),
			IM_COL32(0xF1, 0x4C, 0x4C, 255), IM_COL32(0x23, 0xD1, 0x8B, 255), IM_COL32(0xF5, 0xF5, 0x43, 255),
			IM_COL32(0x3B, 0x8E, 0xEA, 255), IM_COL32(0xD6, 0x70, 0xD6, 255), IM_COL32(0x29, 0xB8, 0xDB, 255),
			IM_COL32(0xFF, 0xFF, 0xFF, 255)};

		ImU32 PaletteColor(uint8_t index, bool bold)
		{
			if (bold && index < 8)
				index = static_cast<uint8_t>(index + 8);
			return kAnsiPalette[index & 0x0F];
		}

		struct KeyMapping
		{
			ImGuiKey key;
			bool needsCtrl;
			const char *bytes;
		};

		// Enough to make bash/PSReadLine-style line editing work (arrows, Tab-completion, Ctrl+C
		// to interrupt, ...) - not a full terminfo keymap. ponytail: no Ctrl+V paste, no
		// Shift+arrow selection; add if a session actually needs them.
		constexpr KeyMapping kKeyMappings[] = {
			{ImGuiKey_Enter, false, "\r"},
			{ImGuiKey_KeypadEnter, false, "\r"},
			{ImGuiKey_Backspace, false, "\x7F"},
			{ImGuiKey_Tab, false, "\t"},
			{ImGuiKey_Escape, false, "\x1B"},
			{ImGuiKey_UpArrow, false, "\x1B[A"},
			{ImGuiKey_DownArrow, false, "\x1B[B"},
			{ImGuiKey_RightArrow, false, "\x1B[C"},
			{ImGuiKey_LeftArrow, false, "\x1B[D"},
			{ImGuiKey_Home, false, "\x1B[H"},
			{ImGuiKey_End, false, "\x1B[F"},
			{ImGuiKey_Delete, false, "\x1B[3~"},
			{ImGuiKey_PageUp, false, "\x1B[5~"},
			{ImGuiKey_PageDown, false, "\x1B[6~"},
			{ImGuiKey_C, true, "\x03"},
			{ImGuiKey_D, true, "\x04"},
			{ImGuiKey_L, true, "\x0C"},
			{ImGuiKey_A, true, "\x01"},
			{ImGuiKey_E, true, "\x05"},
		};
	} // namespace

	TerminalPanel::TerminalPanel(std::string title, bool visibleByDefault)
		: IPanel(std::move(title), visibleByDefault)
	{
	}

	Ref<IPanel> TerminalPanel::Clone() const
	{
		// A fresh panel with its own tabs/panes, not a copy of this one's sessions -
		// ConPtyProcess isn't copyable (one pseudo console, one child), and "clone" here means
		// "open another dockable group of shells".
		return CreateRef<TerminalPanel>(GetTitle());
	}

	void TerminalPanel::RequestFocus()
	{
		SetVisible(true);
		m_FocusRequested = true;
	}

	void TerminalPanel::OpenNewTab()
	{
		Unique<Tab> tab = CreateUnique<Tab>();
		tab->id = m_NextTabId++;
		tab->label = "Shell " + std::to_string(tab->id);
		Unique<Session> session = CreateUnique<Session>();
		session->id = m_NextSessionId++;
		tab->panes.push_back(std::move(session));
		tab->wantsSelect = true;
		m_Tabs.push_back(std::move(tab));
		RequestFocus();
	}

	void TerminalPanel::CloseActiveTab()
	{
		if (m_Tabs.empty())
			return;

		const int tabIndex = std::clamp(m_ActiveTabIndex, 0, static_cast<int>(m_Tabs.size()) - 1);
		Tab &tab = *m_Tabs[static_cast<std::size_t>(tabIndex)];
		const int paneIndex = std::clamp(tab.focusedPaneIndex, 0, static_cast<int>(tab.panes.size()) - 1);

		// Session's destructor (via ConPtyProcess::~ConPtyProcess) kills the child shell and
		// joins its reader thread - no explicit cleanup needed.
		tab.panes.erase(tab.panes.begin() + paneIndex);

		if (tab.panes.empty())
			m_Tabs.erase(m_Tabs.begin() + tabIndex);
		else if (tab.focusedPaneIndex >= static_cast<int>(tab.panes.size()))
			tab.focusedPaneIndex = static_cast<int>(tab.panes.size()) - 1;
	}

	void TerminalPanel::SplitActivePane()
	{
		if (m_Tabs.empty())
		{
			OpenNewTab();
			return;
		}

		const int tabIndex = std::clamp(m_ActiveTabIndex, 0, static_cast<int>(m_Tabs.size()) - 1);
		Tab &tab = *m_Tabs[static_cast<std::size_t>(tabIndex)];
		Unique<Session> session = CreateUnique<Session>();
		session->id = m_NextSessionId++;
		tab.panes.push_back(std::move(session));
		tab.focusedPaneIndex = static_cast<int>(tab.panes.size()) - 1;
		RequestFocus();
	}

	void TerminalPanel::ensureStarted(Session &session)
	{
		if (session.startAttempted)
			return;
		session.startAttempted = true;

		Platform::ConPtyProcessOptions options;
		options.executable = Path("powershell.exe");
		options.arguments = {"-NoLogo", "-NoExit"};
		options.workingDirectory = FileSystem::CurrentPath();
		options.columns = session.columns;
		options.rows = session.rows;

		VoidResult started = session.process.Start(options);
		if (!started.HasValue())
			session.startError = started.Error().technicalDetails;
	}

	void TerminalPanel::forwardKeyboardInput(Session &session)
	{
		ImGuiIO &io = ImGui::GetIO();

		// Ctrl+Shift+<key> and Ctrl+` are reserved for terminal-management shortcuts (new
		// tab/close/split/focus - see keybindings.yaml), never shell input. Leave
		// WantCaptureKeyboard untouched for these so Application::OnEvent's KeymapResolver
		// dispatch still sees the keypress next frame - otherwise those shortcuts could never
		// fire while the terminal is focused.
		if (io.KeyCtrl && (io.KeyShift || ImGui::IsKeyDown(ImGuiKey_GraveAccent)))
			return;

		// Claims the keyboard for this frame so app-wide shortcuts (single-letter view/axis
		// commands) don't fire on every keystroke typed at the shell - same pattern and same
		// one-frame-lag caveat as RendererPanel's fallbackGizmoDragging flag.
		io.WantCaptureKeyboard = true;

		for (int i = 0; i < io.InputQueueCharacters.Size; ++i)
		{
			const ImWchar character = io.InputQueueCharacters[i];
			if (character == 0)
				continue;

			char utf8[5] = {};
			ImTextCharToUtf8(utf8, static_cast<unsigned int>(character));
			session.process.WriteRaw(utf8);
		}

		for (const KeyMapping &mapping : kKeyMappings)
		{
			if (mapping.needsCtrl != io.KeyCtrl)
				continue;
			if (ImGui::IsKeyPressed(mapping.key, true))
				session.process.WriteRaw(mapping.bytes);
		}
	}

	bool TerminalPanel::renderSession(Session &session, float width, bool focusRequested)
	{
		ensureStarted(session);
		session.screen.Feed(session.process.PollOutput());

		if (!session.startError.empty())
		{
			ImGui::BeginChild("Pane", ImVec2(width, 0.0f), true);
			ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", session.startError.c_str());
			ImGui::EndChild();
			return false;
		}

		ImFont *monospaceFont = GetEditorMonospaceFont();
		if (monospaceFont != nullptr)
			ImGui::PushFont(monospaceFont);
		const ImVec2 glyphSize = ImGui::CalcTextSize("M");

		const ImVec2 available(width, ImGui::GetContentRegionAvail().y);
		const int columns = std::max(1, static_cast<int>(available.x / glyphSize.x));
		const int rows = std::max(1, static_cast<int>(available.y / glyphSize.y));
		if (columns != session.columns || rows != session.rows)
		{
			session.columns = columns;
			session.rows = rows;
			session.screen.Resize(columns, rows);
			session.process.Resize(columns, rows);
		}

		ImGui::BeginChild(
			"Grid", ImVec2(width, 0.0f), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
		if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
			ImGui::SetWindowFocus();
		if (focusRequested)
			ImGui::SetWindowFocus();
		const bool isFocused = ImGui::IsWindowFocused();
		if (isFocused)
			forwardKeyboardInput(session);

		ImDrawList *drawList = ImGui::GetWindowDrawList();
		const ImVec2 origin = ImGui::GetCursorScreenPos();
		drawList->AddRectFilled(origin, ImVec2(origin.x + available.x, origin.y + available.y), IM_COL32(0, 0, 0, 255));

		char glyph[5] = {};
		for (int row = 0; row < session.screen.Rows(); ++row)
		{
			for (int col = 0; col < session.screen.Columns(); ++col)
			{
				const VtCell &cell = session.screen.CellAt(row, col);
				const ImVec2 cellPos(origin.x + col * glyphSize.x, origin.y + row * glyphSize.y);
				if (cell.background != 0)
				{
					drawList->AddRectFilled(
						cellPos, ImVec2(cellPos.x + glyphSize.x, cellPos.y + glyphSize.y),
						PaletteColor(cell.background, false));
				}
				if (cell.codepoint != U' ' && cell.codepoint != 0)
				{
					const int length = ImTextCharToUtf8(glyph, static_cast<unsigned int>(cell.codepoint));
					drawList->AddText(cellPos, PaletteColor(cell.foreground, cell.bold), glyph, glyph + length);
				}
			}
		}

		if (std::fmod(ImGui::GetTime(), 1.0) < 0.5)
		{
			const ImVec2 cursorPos(
				origin.x + session.screen.CursorColumn() * glyphSize.x, origin.y + session.screen.CursorRow() * glyphSize.y);
			drawList->AddRectFilled(
				cursorPos, ImVec2(cursorPos.x + glyphSize.x, cursorPos.y + glyphSize.y), IM_COL32(200, 200, 200, 120));
		}

		ImGui::EndChild();
		if (monospaceFont != nullptr)
			ImGui::PopFont();
		return isFocused;
	}

	void TerminalPanel::renderTab(Tab &tab)
	{
		const float available = ImGui::GetContentRegionAvail().x;
		const float paneWidth = available / static_cast<float>(tab.panes.size());
		const bool isSplit = tab.panes.size() > 1;
		int paneToClose = -1;

		for (std::size_t paneIndex = 0; paneIndex < tab.panes.size(); ++paneIndex)
		{
			Session &session = *tab.panes[paneIndex];
			ImGui::PushID(session.id);
			ImGui::BeginGroup();
			// Only shown in split view - a lone pane already has the tab bar's own "x" to close
			// it, and killing "the terminal" there is unambiguous.
			if (isSplit)
			{
				ImGui::TextDisabled("Pane %d", static_cast<int>(paneIndex + 1));
				ImGui::SameLine(paneWidth - 24.0f);
				if (ImGui::SmallButton("x"))
					paneToClose = static_cast<int>(paneIndex);
			}
			const bool focusThisPane = m_FocusRequested && paneIndex == static_cast<std::size_t>(tab.focusedPaneIndex);
			if (renderSession(session, paneWidth, focusThisPane))
				tab.focusedPaneIndex = static_cast<int>(paneIndex);
			ImGui::EndGroup();
			ImGui::PopID();
			if (paneIndex + 1 < tab.panes.size())
				ImGui::SameLine();
		}

		if (paneToClose >= 0)
		{
			// Session's destructor (ConPtyProcess::~ConPtyProcess) kills the child shell - see
			// CloseActiveTab. isSplit guarantees at least one pane remains, so the tab itself
			// never needs closing here.
			tab.panes.erase(tab.panes.begin() + paneToClose);
			if (tab.focusedPaneIndex >= static_cast<int>(tab.panes.size()))
				tab.focusedPaneIndex = static_cast<int>(tab.panes.size()) - 1;
		}
	}

	void TerminalPanel::Render()
	{
		if (!IsVisible())
			return;

		if (m_Tabs.empty())
			OpenNewTab();

		bool windowOpen = true;
		if (!ImGui::Begin((GetTitle() + "###TerminalPanel").c_str(), &windowOpen))
		{
			ImGui::End();
			SetVisible(windowOpen);
			return;
		}
		if (m_FocusRequested)
			ImGui::SetWindowFocus();

		if (ImGui::BeginTabBar("Tabs", ImGuiTabBarFlags_Reorderable | ImGuiTabBarFlags_FittingPolicyResizeDown))
		{
			for (std::size_t index = 0; index < m_Tabs.size();)
			{
				Tab &tab = *m_Tabs[index];
				bool tabOpen = true;
				ImGuiTabItemFlags flags = ImGuiTabItemFlags_None;
				if (tab.wantsSelect)
				{
					flags |= ImGuiTabItemFlags_SetSelected;
					tab.wantsSelect = false;
				}

				ImGui::PushID(tab.id);
				if (ImGui::BeginTabItem(tab.label.c_str(), &tabOpen, flags))
				{
					m_ActiveTabIndex = static_cast<int>(index);
					renderTab(tab);
					ImGui::EndTabItem();
				}
				ImGui::PopID();

				if (!tabOpen)
				{
					m_Tabs.erase(m_Tabs.begin() + static_cast<std::ptrdiff_t>(index));
					continue;
				}
				++index;
			}

			if (ImGui::TabItemButton("+", ImGuiTabItemFlags_Trailing | ImGuiTabItemFlags_NoTooltip))
				OpenNewTab();
			if (ImGui::TabItemButton("Split", ImGuiTabItemFlags_Trailing | ImGuiTabItemFlags_NoTooltip))
				SplitActivePane();

			ImGui::EndTabBar();
		}

		m_FocusRequested = false;
		ImGui::End();
		SetVisible(windowOpen);
	}
} // namespace DefectStudio
