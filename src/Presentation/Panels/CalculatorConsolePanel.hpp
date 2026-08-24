#pragma once

#include <array>
#include <string>
#include <vector>

#include <imgui.h>

#include "Core/Platform/InteractiveProcess.hpp"
#include "Presentation/Panels/IPanel.hpp"

namespace DefectStudio
{
	// Dockable IPython-backed calculator console - deliberately NOT a terminal (see TerminalPanel
	// for that): plain InteractiveProcess pipes, no VT100/ConPTY, no ANSI rendering. IPython's
	// `--simple-prompt` mode exists precisely for this "driven over a plain pipe" case - it prints
	// bare `In [1]:`/`Out[1]:` text and still accumulates multi-line blocks (if/for/def) itself
	// before executing, so this panel never needs to parse Python syntax. Always the app's own
	// `.venv` (never system Python) - see
	// docs/work/project/plans/2026-08-24-calc-tools.md section 4.
	class CalculatorConsolePanel final : public IPanel
	{
	public:
		explicit CalculatorConsolePanel(std::string title = "Calculator", bool visibleByDefault = false);

		void Render() override;
		[[nodiscard]] Ref<IPanel> Clone() const override;

	private:
		void ensureStarted();
		static int HistoryCallback(ImGuiInputTextCallbackData *data);

		Platform::InteractiveProcess m_Process;
		std::string m_Output;
		std::string m_StartError;
		bool m_StartAttempted = false;

		std::array<char, 4096> m_InputBuffer{};
		std::vector<std::string> m_History;
		int m_HistoryCursor = -1;
	};
} // namespace DefectStudio
