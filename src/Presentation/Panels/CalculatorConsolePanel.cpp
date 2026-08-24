#include "Core/dspch.hpp"

#include "Presentation/Panels/CalculatorConsolePanel.hpp"

#include "Core/Platform/PlatformPythonRuntime.hpp"
#include "Presentation/EditorFonts.hpp"

namespace DefectStudio
{
	CalculatorConsolePanel::CalculatorConsolePanel(std::string title, bool visibleByDefault)
		: IPanel(std::move(title), visibleByDefault)
	{
	}

	Ref<IPanel> CalculatorConsolePanel::Clone() const
	{
		// A fresh panel with its own process, not a copy - InteractiveProcess isn't copyable (one
		// pipe pair, one child), same reasoning as TerminalPanel::Clone.
		return CreateRef<CalculatorConsolePanel>(GetTitle());
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
			m_StartError = started.Error().technicalDetails;
	}

	int CalculatorConsolePanel::HistoryCallback(ImGuiInputTextCallbackData *data)
	{
		if (data->EventFlag != ImGuiInputTextFlags_CallbackHistory)
			return 0;

		auto *self = static_cast<CalculatorConsolePanel *>(data->UserData);
		const int previousCursor = self->m_HistoryCursor;
		const int historySize = static_cast<int>(self->m_History.size());

		if (data->EventKey == ImGuiKey_UpArrow)
		{
			if (self->m_HistoryCursor == -1)
				self->m_HistoryCursor = historySize - 1;
			else if (self->m_HistoryCursor > 0)
				--self->m_HistoryCursor;
		}
		else if (data->EventKey == ImGuiKey_DownArrow && self->m_HistoryCursor != -1)
		{
			if (++self->m_HistoryCursor >= historySize)
				self->m_HistoryCursor = -1;
		}

		if (previousCursor != self->m_HistoryCursor)
		{
			const std::string replacement =
				self->m_HistoryCursor >= 0 ? self->m_History[static_cast<std::size_t>(self->m_HistoryCursor)] : std::string();
			data->DeleteChars(0, data->BufTextLen);
			data->InsertChars(0, replacement.c_str());
		}
		return 0;
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

		ensureStarted();
		m_Output += m_Process.PollOutput();

		if (!m_StartError.empty())
		{
			ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", m_StartError.c_str());
			ImGui::End();
			SetVisible(windowOpen);
			return;
		}

		ImFont *monospaceFont = GetEditorMonospaceFont();
		if (monospaceFont != nullptr)
			ImGui::PushFont(monospaceFont);

		const float inputHeight = ImGui::GetFrameHeightWithSpacing();
		if (ImGui::BeginChild("Output", ImVec2(0.0f, -inputHeight), true))
		{
			ImGui::TextUnformatted(m_Output.c_str(), m_Output.c_str() + m_Output.size());
			if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 5.0f)
				ImGui::SetScrollHereY(1.0f);
		}
		ImGui::EndChild();

		ImGui::SetNextItemWidth(-1.0f);
		constexpr ImGuiInputTextFlags flags = ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackHistory;
		bool reclaimFocus = ImGui::IsWindowAppearing();
		if (ImGui::InputText(
				"##CalculatorInput", m_InputBuffer.data(), m_InputBuffer.size(), flags, &HistoryCallback, this))
		{
			const std::string line(m_InputBuffer.data());
			// No local echo over a plain pipe (unlike a real tty) - we have to print what we typed
			// ourselves so the transcript still reads like a normal REPL session.
			m_Output += line + "\n";
			m_Process.WriteLine(line);
			if (!line.empty())
				m_History.push_back(line);
			m_HistoryCursor = -1;
			m_InputBuffer[0] = '\0';
			reclaimFocus = true;
		}
		if (reclaimFocus)
			ImGui::SetKeyboardFocusHere(-1);

		if (monospaceFont != nullptr)
			ImGui::PopFont();

		ImGui::End();
		SetVisible(windowOpen);
	}
} // namespace DefectStudio
