#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "Core/Diagnostics/StructuredError.hpp"
#include "Core/Utils/Path.hpp"

namespace DefectStudio::Platform
{
	struct InteractiveProcessOptions
	{
		Path executable;
		std::vector<std::string> arguments;
		Path workingDirectory;
	};

	// Sibling to ProcessRunner, not a replacement: ProcessRunner blocks for one final result,
	// this stays alive across many round trips (shell session, REPL) with non-blocking reads on
	// a background thread. stdout and stderr share one pipe (like a real terminal) so interleaved
	// output arrives in the order the child actually wrote it. Windows-only for now - nothing
	// needs a POSIX path yet, see docs/work/project/plans/2026-08-24-calc-tools.md section 2/3.
	class InteractiveProcess
	{
	public:
		InteractiveProcess() = default;
		~InteractiveProcess();

		InteractiveProcess(const InteractiveProcess &) = delete;
		InteractiveProcess &operator=(const InteractiveProcess &) = delete;

		[[nodiscard]] VoidResult Start(const InteractiveProcessOptions &options);

		// Appends '\n' and writes to the child's stdin. No-op if not running.
		void WriteLine(const std::string &line);

		// Drains and returns everything read since the last call. Not split into lines - a
		// prompt with no trailing newline (e.g. "PS C:\>") still needs to show up immediately.
		[[nodiscard]] std::string PollOutput();

		[[nodiscard]] bool IsRunning() const;

		// Kills the child if still alive and joins the reader thread. Safe to call more than
		// once and safe to skip if the child already exited on its own.
		void Terminate();

	private:
		void readLoop();

		void *m_StdinWrite = nullptr;
		void *m_StdoutRead = nullptr;
		void *m_ProcessHandle = nullptr;

		std::thread m_ReaderThread;
		std::mutex m_OutputMutex;
		std::string m_OutputBuffer;
		std::atomic<bool> m_Running{false};
	};
} // namespace DefectStudio::Platform
