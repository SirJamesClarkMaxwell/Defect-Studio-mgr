#include "Core/dspch.hpp"

#include "Presentation/Panels/CalculatorConsolePanel.hpp"

#include <algorithm>

#include "Core/Platform/PlatformPythonRuntime.hpp"
#include "Presentation/EditorFonts.hpp"

namespace DefectStudio
{
	namespace
	{
		// Same tones as TerminalPanel's ANSI palette (green/red), kept consistent across the two
		// console-like panels rather than picking new arbitrary colors.
		constexpr ImU32 kInputColor = IM_COL32(0x0D, 0xBC, 0x79, 255);
		constexpr ImU32 kOutputColor = IM_COL32(0xCD, 0x31, 0x31, 255);

		std::string ToPythonStringLiteral(std::string text)
		{
			std::replace(text.begin(), text.end(), '\\', '/');
			std::string escaped;
			escaped.reserve(text.size() + 2);
			escaped += '"';
			for (const char ch : text)
			{
				if (ch == '"' || ch == '\\')
					escaped += '\\';
				escaped += ch;
			}
			escaped += '"';
			return escaped;
		}
	} // namespace

	CalculatorConsolePanel::CalculatorConsolePanel(std::string title, bool visibleByDefault)
		: IPanel(std::move(title), visibleByDefault)
	{
		m_InputEditor.SetLanguage(TextEditor::Language::Python());
	}

	Ref<IPanel> CalculatorConsolePanel::Clone() const
	{
		// A fresh panel with its own process, not a copy - InteractiveProcess isn't copyable (one
		// pipe pair, one child), same reasoning as TerminalPanel::Clone.
		return CreateRef<CalculatorConsolePanel>(GetTitle());
	}

	void CalculatorConsolePanel::SetProjectContext(std::string projectRoot, std::vector<std::string> projectRoots)
	{
		m_ProjectRoot = std::move(projectRoot);
		m_ProjectRoots = std::move(projectRoots);
	}

	void CalculatorConsolePanel::ensureStarted()
	{
		if (m_StartAttempted)
			return;
		m_StartAttempted = true;

		const Path venvPython = Platform::ResolveVenvPythonExecutable();
		if (!FileSystem::Exists(venvPython.Native()))
		{
			m_StartError = "No .venv found - run scripts/python/setup.py (or the bootstrap wrapper) first.";
			return;
		}

#if defined(DS_PLATFORM_WINDOWS)
		const Path ipython = venvPython.parent_path() / "ipython.exe";
#else
		const Path ipython = venvPython.parent_path() / "ipython";
#endif

		Platform::InteractiveProcessOptions options;
		options.workingDirectory = FileSystem::CurrentPath();
		if (FileSystem::Exists(ipython.Native()))
		{
			// --simple-prompt: IPython's own mode for exactly this "driven over a plain pipe, no
			// real terminal" case - bare "In [1]:"/"Out[1]:" text, no readline/ANSI, but still
			// accumulates multi-line if/for/def blocks itself before executing. That's why this
			// panel never has to parse Python syntax on its own side.
			options.executable = ipython;
			options.arguments = {"--simple-prompt", "--no-banner"};
		}
		else
		{
			// Fallback if ipython wasn't installed into the venv - plain python's own interactive
			// loop accumulates multi-line blocks the same way, just without IPython's In/Out
			// numbering.
			options.executable = venvPython;
			options.arguments = {"-i"};
		}

		VoidResult started = m_Process.Start(options);
		if (!started.HasValue())
		{
			m_StartError = started.Error().technicalDetails;
			return;
		}

		if (m_ProjectRoot.empty() && m_ProjectRoots.empty())
			return;

		const std::string projectRootLine =
			"project_root = " + (m_ProjectRoot.empty() ? std::string("None") : ToPythonStringLiteral(m_ProjectRoot));

		std::string projectRootsLine = "project_roots = [";
		for (std::size_t i = 0; i < m_ProjectRoots.size(); ++i)
		{
			if (i != 0)
				projectRootsLine += ", ";
			projectRootsLine += ToPythonStringLiteral(m_ProjectRoots[i]);
		}
		projectRootsLine += "]";

		m_Process.WriteLine(projectRootLine);
		m_Process.WriteLine(projectRootsLine);

		appendSegment(
			"# injected by Defect Studio - see docs/work/project/plans/2026-08-24-calc-tools.md section 4\n" +
				projectRootLine + "\n" + projectRootsLine + "\n",
			true);
	}

	void CalculatorConsolePanel::appendSegment(std::string text, bool isUserInput)
	{
		if (text.empty())
			return;
		if (!m_Segments.empty() && m_Segments.back().isUserInput == isUserInput)
			m_Segments.back().text += text;
		else
			m_Segments.push_back(Segment{std::move(text), isUserInput});
	}

	void CalculatorConsolePanel::submitCurrentCell()
	{
		std::string trimmed = m_InputEditor.GetText();
		// The editor reports a trailing newline for its implicit empty last line - drop it so
		// splitting below doesn't produce a spurious empty final WriteLine.
		if (!trimmed.empty() && trimmed.back() == '\n')
			trimmed.pop_back();
		if (trimmed.find_first_not_of(" \t\r\n") == std::string::npos)
			return;

		const bool isMultiLine = trimmed.find('\n') != std::string::npos;

		std::size_t start = 0;
		while (start <= trimmed.size())
		{
			const std::size_t end = trimmed.find('\n', start);
			m_Process.WriteLine(trimmed.substr(start, end == std::string::npos ? std::string::npos : end - start));
			if (end == std::string::npos)
				break;
			start = end + 1;
		}
		// A single statement never needs this - IPython executes it as soon as that one line
		// arrives. A real if/for/def block does: without a trailing blank line it stays "...:",
		// waiting for one more (possibly empty) line before it'll run. Only sending this for
		// multi-line cells avoids provoking IPython into reprinting the prompt for no reason on
		// the (overwhelmingly common) single-line case.
		if (isMultiLine)
			m_Process.WriteLine("");

		appendSegment(trimmed + "\n", true);
		m_InputEditor.SetText("");
		m_InputEditor.SetFocus();
	}

	void CalculatorConsolePanel::Render()
	{
		if (!IsVisible())
			return;

		bool windowOpen = true;
		if (!ImGui::Begin((GetTitle() + "###CalculatorPanel").c_str(), &windowOpen))
		{
			ImGui::End();
			SetVisible(windowOpen);
			return;
		}

		if (ImGui::IsWindowAppearing())
			m_InputEditor.SetFocus();

		ensureStarted();
		std::string polled = m_Process.PollOutput();
		if (!polled.empty())
			m_ReceivedFirstOutput = true;
		appendSegment(std::move(polled), false);

		if (!m_StartError.empty())
		{
			ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", m_StartError.c_str());
			ImGui::End();
			SetVisible(windowOpen);
			return;
		}

		if (ImGui::SmallButton("Clear"))
			m_Segments.clear();

		ImFont *monospaceFont = GetEditorMonospaceFont();
		if (monospaceFont != nullptr)
			ImGui::PushFont(monospaceFont);

		// Input cell grows with its content (Jupyter/VSCode Interactive Window style), clamped to
		// a sane range so a long paste doesn't swallow the whole panel.
		const float lineHeight = ImGui::GetTextLineHeightWithSpacing();
		const int inputRows = std::clamp(static_cast<int>(m_InputEditor.GetLineCount()) + 1, 3, 12);
		const float inputHeight = lineHeight * static_cast<float>(inputRows);

		if (ImGui::BeginChild("Transcript", ImVec2(0.0f, -inputHeight), true))
		{
			// Deliberately not trying to keep e.g. "In [3]: " and the echoed code on the same
			// visual row (which needs matching ImGui::SameLine calls keyed on trailing-newline
			// state) - proved fragile under real typing/process-startup timing, garbling into
			// fused, misordered text. Each segment gets its own line; less exactly REPL-shaped,
			// reliably correct.
			for (const Segment &segment : m_Segments)
			{
				ImGui::PushStyleColor(ImGuiCol_Text, segment.isUserInput ? kInputColor : kOutputColor);
				ImGui::TextUnformatted(segment.text.c_str(), segment.text.c_str() + segment.text.size());
				ImGui::PopStyleColor();
			}
			if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 5.0f)
				ImGui::SetScrollHereY(1.0f);
		}
		ImGui::EndChild();

		// Ctrl+Enter or Shift+Enter submits the cell (both are "run this cell" in Jupyter/VSCode -
		// we don't distinguish "advance to a new cell" from "stay put", so both just submit). Plain
		// Enter stays the editor's own "insert newline", needed for multi-line if/for/def blocks.
		m_InputEditor.Render("ConsoleInput", ImVec2(0.0f, inputHeight));
		const bool focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
		const ImGuiIO &io = ImGui::GetIO();
		const bool enterPressed = ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false);
		// Guarded on having seen IPython's first prompt - submitting any earlier (process just
		// spawned, cold start can take a moment) would show the echoed input before anything
		// invited it. Deliberately NOT `!m_Segments.empty()`: Clear empties that vector without the
		// session becoming any less ready.
		if (focused && m_ReceivedFirstOutput && (io.KeyCtrl || io.KeyShift) && enterPressed)
			submitCurrentCell();

		if (monospaceFont != nullptr)
			ImGui::PopFont();

		ImGui::End();
		SetVisible(windowOpen);
	}
} // namespace DefectStudio
