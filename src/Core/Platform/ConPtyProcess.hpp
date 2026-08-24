#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "Core/Diagnostics/StructuredError.hpp"
#include "Core/Utils/Path.hpp"

namespace DefectStudio::Platform
{
	struct ConPtyProcessOptions
	{
		Path executable;
		std::vector<std::string> arguments;
		Path workingDirectory;
		int columns = 80;
		int rows = 24;
	};

	// Like InteractiveProcess, but backs the child with a real Windows pseudo console (ConPTY)
	// instead of plain pipes. Needed for anything that wants to behave like an actual terminal:
	// inline line editing, Tab completion, ANSI colors. A plain pipe can't do this - PSReadLine/
	// GNU readline only turn on interactive line editing when attached to a console, not a bare
	// pipe. VtScreen (Presentation/Terminal/VtScreen.hpp) turns the raw byte stream this produces
	// back into a character grid for rendering. Windows-only (ConPTY is a Win32 API); nothing
	// needs a POSIX path yet.
	class ConPtyProcess
	{
	public:
		ConPtyProcess() = default;
		~ConPtyProcess();

		ConPtyProcess(const ConPtyProcess &) = delete;
		ConPtyProcess &operator=(const ConPtyProcess &) = delete;

		[[nodiscard]] VoidResult Start(const ConPtyProcessOptions &options);

		// Sent verbatim to the pseudo console's input - no implicit newline, unlike
		// InteractiveProcess::WriteLine. Callers forward raw keystrokes (printable chars, "\r"
		// for Enter, escape sequences for arrow keys, ...), not whole commands.
		void WriteRaw(std::string_view bytes);

		void Resize(int columns, int rows);

		// Drains and returns everything read since the last call - raw bytes, ANSI escape
		// sequences included, meant to be fed into a VtScreen.
		[[nodiscard]] std::string PollOutput();

		[[nodiscard]] bool IsRunning() const;

		void Terminate();

	private:
		void readLoop();

		void *m_PseudoConsole = nullptr;
		void *m_InputWrite = nullptr;
		void *m_OutputRead = nullptr;
		void *m_ProcessHandle = nullptr;

		std::thread m_ReaderThread;
		std::mutex m_OutputMutex;
		std::string m_OutputBuffer;
		std::atomic<bool> m_Running{false};
	};
} // namespace DefectStudio::Platform
