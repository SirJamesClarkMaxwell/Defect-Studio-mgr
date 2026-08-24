#include "Core/dspch.hpp"

#include "Core/Platform/InteractiveProcess.hpp"

#if defined(DS_PLATFORM_WINDOWS)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "Core/Platform/Internal/Win32ProcessSupport.hpp"

namespace DefectStudio::Platform
{
	namespace
	{
		StructuredError MakeInteractiveProcessError(std::string technicalDetails, std::string code)
		{
			return StructuredError{
				ErrorCategory::Runtime,
				Severity::Error,
				"Failed to start interactive process.",
				std::move(technicalDetails),
				"Verify executable path and working directory.",
				"Core/Platform/InteractiveProcess",
				std::move(code)};
		}
	} // namespace

	InteractiveProcess::~InteractiveProcess()
	{
		Terminate();
	}

	VoidResult InteractiveProcess::Start(const InteractiveProcessOptions &options)
	{
		if (options.executable.Empty())
			return MakeInteractiveProcessError("Executable path is empty.", "interactive_process.executable_missing");
		if (m_ProcessHandle != nullptr)
			return MakeInteractiveProcessError("Process already started.", "interactive_process.already_started");

		Internal::PipeHandles stdinPipe;
		Internal::PipeHandles stdoutPipe;
		if (!Internal::CreateInheritablePipe(stdinPipe) || !Internal::CreateInheritablePipe(stdoutPipe))
			return MakeInteractiveProcessError("CreatePipe failed.", "interactive_process.pipe_failed");
		if (!SetHandleInformation(stdinPipe.write, HANDLE_FLAG_INHERIT, 0) ||
			!SetHandleInformation(stdoutPipe.read, HANDLE_FLAG_INHERIT, 0))
			return MakeInteractiveProcessError("SetHandleInformation failed.", "interactive_process.pipe_failed");

		STARTUPINFOW startupInfo{};
		startupInfo.cb = sizeof(STARTUPINFOW);
		startupInfo.dwFlags = STARTF_USESTDHANDLES;
		startupInfo.hStdInput = stdinPipe.read;
		// stdout and stderr share one write handle - see class comment on InteractiveProcess.
		startupInfo.hStdOutput = stdoutPipe.write;
		startupInfo.hStdError = stdoutPipe.write;

		PROCESS_INFORMATION processInfo{};
		std::wstring commandLine = Internal::ToWideString(Internal::BuildCommandLine(options.executable, options.arguments));
		const std::wstring workingDirectory =
			options.workingDirectory.Empty() ? std::wstring{} : options.workingDirectory.wstring();
		const BOOL created = CreateProcessW(
			nullptr,
			commandLine.data(),
			nullptr,
			nullptr,
			TRUE,
			CREATE_NO_WINDOW,
			nullptr,
			workingDirectory.empty() ? nullptr : workingDirectory.c_str(),
			&startupInfo,
			&processInfo);

		CloseHandle(stdinPipe.read);
		CloseHandle(stdoutPipe.write);

		if (!created)
		{
			CloseHandle(stdinPipe.write);
			CloseHandle(stdoutPipe.read);
			return MakeInteractiveProcessError(
				"CreateProcessW failed with code " + std::to_string(GetLastError()), "interactive_process.create_failed");
		}

		CloseHandle(processInfo.hThread);
		m_StdinWrite = stdinPipe.write;
		m_StdoutRead = stdoutPipe.read;
		m_ProcessHandle = processInfo.hProcess;
		m_Running = true;
		m_ReaderThread = std::thread(&InteractiveProcess::readLoop, this);
		return {};
	}

	void InteractiveProcess::readLoop()
	{
		char buffer[4096];
		DWORD bytesRead = 0;
		while (ReadFile(static_cast<HANDLE>(m_StdoutRead), buffer, static_cast<DWORD>(sizeof(buffer)), &bytesRead, nullptr) &&
			   bytesRead > 0)
		{
			std::lock_guard<std::mutex> lock(m_OutputMutex);
			m_OutputBuffer.append(buffer, buffer + bytesRead);
		}
		m_Running = false;
	}

	void InteractiveProcess::WriteLine(const std::string &line)
	{
		if (!m_Running.load() || m_StdinWrite == nullptr)
			return;

		std::string data = line;
		data.push_back('\n');
		DWORD written = 0;
		WriteFile(static_cast<HANDLE>(m_StdinWrite), data.data(), static_cast<DWORD>(data.size()), &written, nullptr);
	}

	std::string InteractiveProcess::PollOutput()
	{
		std::lock_guard<std::mutex> lock(m_OutputMutex);
		std::string output = std::move(m_OutputBuffer);
		m_OutputBuffer.clear();
		return output;
	}

	bool InteractiveProcess::IsRunning() const
	{
		return m_Running.load();
	}

	void InteractiveProcess::Terminate()
	{
		if (m_ProcessHandle != nullptr)
		{
			// Harmless if the process already exited on its own (WriteFile/TerminateProcess on a
			// dead handle just fails) - closing the pipe's write end is what actually unblocks
			// the reader thread's ReadFile.
			TerminateProcess(static_cast<HANDLE>(m_ProcessHandle), 1);
			WaitForSingleObject(static_cast<HANDLE>(m_ProcessHandle), INFINITE);
		}
		if (m_ReaderThread.joinable())
			m_ReaderThread.join();
		if (m_StdinWrite != nullptr)
		{
			CloseHandle(static_cast<HANDLE>(m_StdinWrite));
			m_StdinWrite = nullptr;
		}
		if (m_StdoutRead != nullptr)
		{
			CloseHandle(static_cast<HANDLE>(m_StdoutRead));
			m_StdoutRead = nullptr;
		}
		if (m_ProcessHandle != nullptr)
		{
			CloseHandle(static_cast<HANDLE>(m_ProcessHandle));
			m_ProcessHandle = nullptr;
		}
		m_Running = false;
	}
} // namespace DefectStudio::Platform
#else
namespace DefectStudio::Platform
{
	InteractiveProcess::~InteractiveProcess() = default;

	VoidResult InteractiveProcess::Start(const InteractiveProcessOptions &)
	{
		return StructuredError{
			ErrorCategory::Runtime,
			Severity::Error,
			"Interactive process is not supported on this platform yet.",
			"InteractiveProcess::Start called on a non-Windows build.",
			"Windows-only for now - see docs/work/project/plans/2026-08-24-calc-tools.md section 2.",
			"Core/Platform/InteractiveProcess",
			"interactive_process.unsupported_platform"};
	}

	void InteractiveProcess::readLoop()
	{
	}

	void InteractiveProcess::WriteLine(const std::string &)
	{
	}

	std::string InteractiveProcess::PollOutput()
	{
		return {};
	}

	bool InteractiveProcess::IsRunning() const
	{
		return false;
	}

	void InteractiveProcess::Terminate()
	{
	}
} // namespace DefectStudio::Platform
#endif
