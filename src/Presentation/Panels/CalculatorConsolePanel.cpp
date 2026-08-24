#include "Core/dspch.hpp"

#include "Presentation/Panels/CalculatorConsolePanel.hpp"

#include <algorithm>
#include <cctype>
#include <string_view>

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

		// Reserved words - complete. Builtins - a curated common subset, not exhaustive (matches
		// what an editor without a real language server can reasonably offer).
		constexpr const char *kPythonKeywords[] = {
			"False", "None", "True", "and", "as", "assert", "async", "await", "break", "class", "continue", "def",
			"del", "elif", "else", "except", "finally", "for", "from", "global", "if", "import", "in", "is",
			"lambda", "nonlocal", "not", "or", "pass", "raise", "return", "try", "while", "with", "yield"};
		constexpr const char *kPythonBuiltins[] = {
			"abs", "all", "any", "bin", "bool", "bytearray", "bytes", "callable", "chr", "classmethod", "compile",
			"complex", "dict", "dir", "divmod", "enumerate", "eval", "exec", "filter", "float", "format",
			"frozenset", "getattr", "globals", "hasattr", "hash", "help", "hex", "id", "input", "int", "isinstance",
			"issubclass", "iter", "len", "list", "locals", "map", "max", "min", "next", "object", "oct", "open",
			"ord", "pow", "print", "property", "range", "repr", "reversed", "round", "set", "setattr", "slice",
			"sorted", "staticmethod", "str", "sum", "super", "tuple", "type", "vars", "zip"};

		// Exact escaping (backslash/quote/newline) - safe for arbitrary code text, unlike
		// ToPythonPathLiteral below which additionally rewrites backslashes to forward slashes
		// (fine, even nicer to read, for a Windows path; would corrupt e.g. a string literal the
		// user typed that happens to contain a backslash).
		std::string ToPythonStringLiteral(std::string_view text)
		{
			std::string escaped;
			escaped.reserve(text.size() + 2);
			escaped += '"';
			for (const char ch : text)
			{
				if (ch == '\n')
				{
					escaped += "\\n";
					continue;
				}
				if (ch == '"' || ch == '\\')
					escaped += '\\';
				escaped += ch;
			}
			escaped += '"';
			return escaped;
		}

		std::string ToPythonPathLiteral(std::string path)
		{
			std::replace(path.begin(), path.end(), '\\', '/');
			return ToPythonStringLiteral(path);
		}
	} // namespace

	CalculatorConsolePanel::CalculatorConsolePanel(std::string title, bool visibleByDefault)
		: IPanel(std::move(title), visibleByDefault)
	{
		m_InputEditor.SetLanguage(TextEditor::Language::Python());
		// Rest of AutoCompleteConfig stays at the library's own documented "like Visual Studio
		// Code" defaults (trigger on typing, Ctrl+Space to force it, ...) - only the suggestion
		// source needs to be ours.
		m_AutoCompleteConfig.callback = [this](TextEditor::AutoCompleteState &state) { provideSuggestions(state); };
		m_InputEditor.SetAutoCompleteConfig(&m_AutoCompleteConfig);
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
		addKnownIdentifier("project_root");
		addKnownIdentifier("project_roots");
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

		// Same names+import path as the app's own bridge scripts (scripts/python/examples/
		// vasp_output_load.py, puntukas_structure_load.py) - wrapped in the same try/except
		// ImportError convention those scripts use, so a venv without puntukas installed just
		// silently skips this instead of dumping a traceback the moment the console opens.
		static constexpr const char *kPuntukasSetupLines[] = {
			"try:", "    import puntukas", "    from puntukas.vasp import VaspOutput, Poscar", "except ImportError:",
			"    pass"};
		std::string puntukasSetup;
		for (const char *line : kPuntukasSetupLines)
		{
			m_Process.WriteLine(line);
			puntukasSetup += line;
			puntukasSetup += '\n';
		}
		m_Process.WriteLine("");
		appendSegment(std::move(puntukasSetup), true);
		addKnownIdentifier("puntukas");
		addKnownIdentifier("VaspOutput");
		addKnownIdentifier("Poscar");

		if (m_ProjectRoot.empty() && m_ProjectRoots.empty())
			return;

		const std::string projectRootLine =
			"project_root = " + (m_ProjectRoot.empty() ? std::string("None") : ToPythonPathLiteral(m_ProjectRoot));

		std::string projectRootsLine = "project_roots = [";
		for (std::size_t i = 0; i < m_ProjectRoots.size(); ++i)
		{
			if (i != 0)
				projectRootsLine += ", ";
			projectRootsLine += ToPythonPathLiteral(m_ProjectRoots[i]);
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

	void CalculatorConsolePanel::addKnownIdentifier(std::string identifier)
	{
		if (identifier.empty())
			return;
		if (std::find(m_KnownIdentifiers.begin(), m_KnownIdentifiers.end(), identifier) == m_KnownIdentifiers.end())
			m_KnownIdentifiers.push_back(std::move(identifier));
	}

	void CalculatorConsolePanel::recordIdentifiersFrom(const std::string &code)
	{
		auto isIdentifierChar = [](char ch) { return std::isalnum(static_cast<unsigned char>(ch)) != 0 || ch == '_'; };

		std::size_t i = 0;
		while (i < code.size())
		{
			if (!std::isalpha(static_cast<unsigned char>(code[i])) && code[i] != '_')
			{
				++i;
				continue;
			}

			const std::size_t wordStart = i;
			while (i < code.size() && isIdentifierChar(code[i]))
				++i;
			const std::string word = code.substr(wordStart, i - wordStart);

			std::size_t after = i;
			while (after < code.size() && code[after] == ' ')
				++after;

			if (word == "def" || word == "class")
			{
				std::size_t nameEnd = after;
				while (nameEnd < code.size() && isIdentifierChar(code[nameEnd]))
					++nameEnd;
				if (nameEnd > after)
					addKnownIdentifier(code.substr(after, nameEnd - after));
			}
			else if (after < code.size() && code[after] == '=' && (after + 1 >= code.size() || code[after + 1] != '='))
			{
				addKnownIdentifier(word);
			}
		}
	}

	void CalculatorConsolePanel::provideSuggestions(TextEditor::AutoCompleteState &state)
	{
		state.suggestions.clear();
		if (state.inComment || state.inString || !state.inIdentifier || state.searchTerm.empty())
			return;

		// Static list only - keywords/builtins/session identifiers (the last populated by
		// recordIdentifiersFrom as the user defines things). No live IPython query: a prior
		// attempt at real `obj.attr` completion asked the running process on every callback
		// invocation, which fired once per render frame the popup stayed open on an unchanged
		// context and flooded the session with wasted In[]/Out[] numbers - reverted rather than
		// chase a debounce/dedup fix mid-session.
		const auto tryAdd = [&](std::string_view candidate) {
			if (candidate.size() > state.searchTerm.size() &&
				candidate.compare(0, state.searchTerm.size(), state.searchTerm) == 0)
				state.suggestions.emplace_back(candidate);
		};
		for (const char *keyword : kPythonKeywords)
			tryAdd(keyword);
		for (const char *builtin : kPythonBuiltins)
			tryAdd(builtin);
		for (const std::string &identifier : m_KnownIdentifiers)
			tryAdd(identifier);
		std::sort(state.suggestions.begin(), state.suggestions.end());
		state.suggestions.erase(
			std::unique(state.suggestions.begin(), state.suggestions.end()), state.suggestions.end());
	}

	void CalculatorConsolePanel::pollProcessOutput()
	{
		std::string output = m_Process.PollOutput();
		if (output.empty())
			return;
		m_ReceivedFirstOutput = true;
		appendSegment(std::move(output), false);
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

		recordIdentifiersFrom(trimmed);
		appendSegment(trimmed + "\n", true);
		if (m_History.empty() || m_History.back() != trimmed)
			m_History.push_back(trimmed);
		m_HistoryCursor = -1;
		m_InputEditor.SetText("");
		m_InputEditor.SetFocus();
	}

	void CalculatorConsolePanel::handleHistoryNavigation()
	{
		if (m_History.empty())
			return;

		const ImGuiIO &io = ImGui::GetIO();
		if (io.KeyCtrl || io.KeyShift || io.KeyAlt)
			return; // Shift+Up/Down is selection, Ctrl+Up/Down is move-to-top/bottom - not ours

		const bool upPressed = ImGui::IsKeyPressed(ImGuiKey_UpArrow, false);
		const bool downPressed = ImGui::IsKeyPressed(ImGuiKey_DownArrow, false);
		if (!upPressed && !downPressed)
			return;

		const std::string currentText = m_InputEditor.GetText();
		if (m_HistoryCursor != -1 && currentText != m_LastRecalledText)
			m_HistoryCursor = -1; // edited since the last recall - this Up/Down is normal editing now

		// Only claim Up/Down when there's nothing to lose: an empty cell has no cursor-navigation
		// or autocomplete-popup meaning to steal (autocomplete only ever triggers on typed
		// identifier characters, none of which are present in an empty cell). Once already
		// browsing, stay claimed regardless of cell content - that content IS ours (see above).
		const bool cellIsEmpty = currentText.find_first_not_of(" \t\r\n") == std::string::npos;
		if (m_HistoryCursor == -1 && !cellIsEmpty)
			return;

		if (downPressed && m_HistoryCursor == -1)
			return; // nothing "newer" to move into when not already browsing

		if (m_HistoryCursor == -1)
			m_HistoryDraft = currentText;

		if (upPressed)
			m_HistoryCursor = m_HistoryCursor == -1 ? static_cast<int>(m_History.size()) - 1 : std::max(0, m_HistoryCursor - 1);
		else if (m_HistoryCursor + 1 >= static_cast<int>(m_History.size()))
		{
			m_HistoryCursor = -1;
			m_LastRecalledText = m_HistoryDraft;
			m_InputEditor.SetText(m_HistoryDraft);
			return;
		}
		else
			++m_HistoryCursor;

		m_LastRecalledText = m_History[static_cast<std::size_t>(m_HistoryCursor)];
		m_InputEditor.SetText(m_LastRecalledText);
		const std::size_t lastLine = m_InputEditor.GetLineCount() - 1;
		m_InputEditor.SetCursor(TextEditor::DocPos(lastLine, m_InputEditor.GetLineText(lastLine).size()));
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
		pollProcessOutput();

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
		else if (focused)
			handleHistoryNavigation();

		if (monospaceFont != nullptr)
			ImGui::PopFont();

		ImGui::End();
		SetVisible(windowOpen);
	}
} // namespace DefectStudio
