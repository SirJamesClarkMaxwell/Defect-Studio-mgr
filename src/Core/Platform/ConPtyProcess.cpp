#include "Core/dspch.hpp"

#include "Core/Platform/ConPtyProcess.hpp"

#if defined(DS_PLATFORM_WINDOWS)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <vector>

#include "Core/Platform/Internal/Win32ProcessSupport.hpp"

namespace DefectStudio::Platform
{
	namespace
	{
		StructuredError MakeConPtyError(std::string technicalDetails, std::string code)
		{
			return StructuredError{
				ErrorCategory::Runtime,
				Severity::Error,
				"Failed to start terminal process.",
				std::move(technicalDetails),
				"Verify executable path and working directory.",
				"Core/Platform/ConPtyProcess",
				std::move(code)};
		}
	} // namespace

	ConPtyProcess::~ConPtyProcess()
	{
		Terminate();
	}

	VoidResult ConPtyProcess::Start(const ConPtyProcessOptions &options)
	{
		if (options.executable.Empty())
			return MakeConPtyError("Executable path is empty.", "conpty.executable_missing");
		if (m_ProcessHandle != nullptr)
			return MakeConPtyError("Process already started.", "conpty.already_started");

		HANDLE inputRead = nullptr;
		HANDLE inputWrite = nullptr;
		HANDLE outputRead = nullptr;
		HANDLE outputWrite = nullptr;
		if (!CreatePipe(&inputRead, &inputWrite, nullptr, 0) || !CreatePipe(&outputRead, &outputWrite, nullptr, 0))
			return MakeConPtyError("CreatePipe failed.", "conpty.pipe_failed");

		const COORD size{
			static_cast<SHORT>(std::max(options.columns, 1)), static_cast<SHORT>(std::max(options.rows, 1))};
		HPCON pseudoConsole = nullptr;
		const HRESULT createResult = CreatePseudoConsole(size, inputRead, outputWrite, 0, &pseudoConsole);

		if (FAILED(createResult))
		{
			CloseHandle(inputRead);
			CloseHandle(inputWrite);
			CloseHandle(outputRead);
			CloseHandle(outputWrite);
			return MakeConPtyError("CreatePseudoConsole failed with HRESULT " + std::to_string(createResult), "conpty.create_failed");
		}

		STARTUPINFOEXW startupInfo{};
		startupInfo.StartupInfo.cb = sizeof(STARTUPINFOEXW);

		std::size_t attributeListSize = 0;
		InitializeProcThreadAttributeList(nullptr, 1, 0, &attributeListSize);
		std::vector<char> attributeListStorage(attributeListSize);
		startupInfo.lpAttributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributeListStorage.data());
		if (!InitializeProcThreadAttributeList(startupInfo.lpAttributeList, 1, 0, &attributeListSize))
		{
			ClosePseudoConsole(pseudoConsole);
			CloseHandle(inputRead);
			CloseHandle(inputWrite);
			CloseHandle(outputRead);
			CloseHandle(outputWrite);
			return MakeConPtyError(
				"InitializeProcThreadAttributeList failed with code " + std::to_string(GetLastError()), "conpty.attribute_list_failed");
		}
		if (!UpdateProcThreadAttribute(
				startupInfo.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE, pseudoConsole, sizeof(HPCON), nullptr, nullptr))
		{
			DeleteProcThreadAttributeList(startupInfo.lpAttributeList);
			ClosePseudoConsole(pseudoConsole);
			CloseHandle(inputRead);
			CloseHandle(inputWrite);
			CloseHandle(outputRead);
			CloseHandle(outputWrite);
			return MakeConPtyError(
				"UpdateProcThreadAttribute failed with code " + std::to_string(GetLastError()), "conpty.attribute_update_failed");
		}

		PROCESS_INFORMATION processInfo{};
		std::wstring commandLine = Internal::ToWideString(Internal::BuildCommandLine(options.executable, options.arguments));
		const std::wstring workingDirectory =
			options.workingDirectory.Empty() ? std::wstring{} : options.workingDirectory.wstring();
		const BOOL created = CreateProcessW(
			nullptr,
			commandLine.data(),
			nullptr,
			nullptr,
			FALSE,
			EXTENDED_STARTUPINFO_PRESENT,
			nullptr,
			workingDirectory.empty() ? nullptr : workingDirectory.c_str(),
			&startupInfo.StartupInfo,
			&processInfo);

		DeleteProcThreadAttributeList(startupInfo.lpAttributeList);

		if (!created)
		{
			ClosePseudoConsole(pseudoConsole);
			CloseHandle(inputRead);
			CloseHandle(inputWrite);
			CloseHandle(outputRead);
			CloseHandle(outputWrite);
			return MakeConPtyError("CreateProcessW failed with code " + std::to_string(GetLastError()), "conpty.create_failed");
		}

		// Per Microsoft's ConPTY sample, the ends ConPTY owns internally are closed only now -
		// AFTER CreateProcessW, not right after CreatePseudoConsole. Closing them earlier left
		// the child's actual stdio attached to this process's own real console instead of the
		// pseudo console, so its output never reached the capture pipe below.
		CloseHandle(inputRead);
		CloseHandle(outputWrite);

		CloseHandle(processInfo.hThread);
		m_PseudoConsole = pseudoConsole;
		m_InputWrite = inputWrite;
		m_OutputRead = outputRead;
		m_ProcessHandle = processInfo.hProcess;
		m_Running = true;
		m_ReaderThread = std::thread(&ConPtyProcess::readLoop, this);
		return {};
	}

	void ConPtyProcess::readLoop()
	{
		char buffer[4096];
		DWORD bytesRead = 0;
		while (ReadFile(static_cast<HANDLE>(m_OutputRead), buffer, static_cast<DWORD>(sizeof(buffer)), &bytesRead, nullptr) &&
			   bytesRead > 0)
		{
			std::lock_guard<std::mutex> lock(m_OutputMutex);
			m_OutputBuffer.append(buffer, buffer + bytesRead);
		}
		m_Running = false;
	}

	void ConPtyProcess::WriteRaw(std::string_view bytes)
	{
		if (!m_Running.load() || m_InputWrite == nullptr || bytes.empty())
			return;

		DWORD written = 0;
		WriteFile(static_cast<HANDLE>(m_InputWrite), bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr);
	}

	void ConPtyProcess::Resize(int columns, int rows)
	{
		if (m_PseudoConsole == nullptr)
			return;
		const COORD size{static_cast<SHORT>(std::max(columns, 1)), static_cast<SHORT>(std::max(rows, 1))};
		ResizePseudoConsole(static_cast<HPCON>(m_PseudoConsole), size);
	}

	std::string ConPtyProcess::PollOutput()
	{
		std::lock_guard<std::mutex> lock(m_OutputMutex);
		std::string output = std::move(m_OutputBuffer);
		m_OutputBuffer.clear();
		return output;
	}

	bool ConPtyProcess::IsRunning() const
	{
		return m_Running.load();
	}

	void ConPtyProcess::Terminate()
	{
		if (m_PseudoConsole != nullptr)
		{
			// Closes the pseudo console's own handles to the pipes, which is what unblocks the
			// reader thread's ReadFile once the child process (if still alive) exits.
			ClosePseudoConsole(static_cast<HPCON>(m_PseudoConsole));
			m_PseudoConsole = nullptr;
		}
		if (m_ProcessHandle != nullptr)
		{
			TerminateProcess(static_cast<HANDLE>(m_ProcessHandle), 1);
			WaitForSingleObject(static_cast<HANDLE>(m_ProcessHandle), INFINITE);
		}
		if (m_ReaderThread.joinable())
			m_ReaderThread.join();
		if (m_InputWrite != nullptr)
		{
			CloseHandle(static_cast<HANDLE>(m_InputWrite));
			m_InputWrite = nullptr;
		}
		if (m_OutputRead != nullptr)
		{
			CloseHandle(static_cast<HANDLE>(m_OutputRead));
			m_OutputRead = nullptr;
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
	ConPtyProcess::~ConPtyProcess() = default;

	VoidResult ConPtyProcess::Start(const ConPtyProcessOptions &)
	{
		return StructuredError{
			ErrorCategory::Runtime,
			Severity::Error,
			"Terminal process is not supported on this platform yet.",
			"ConPtyProcess::Start called on a non-Windows build.",
			"Windows-only for now (ConPTY is a Win32 API).",
			"Core/Platform/ConPtyProcess",
			"conpty.unsupported_platform"};
	}

	void ConPtyProcess::readLoop()
	{
	}

	void ConPtyProcess::WriteRaw(std::string_view)
	{
	}

	void ConPtyProcess::Resize(int, int)
	{
	}

	std::string ConPtyProcess::PollOutput()
	{
		return {};
	}

	bool ConPtyProcess::IsRunning() const
	{
		return false;
	}

	void ConPtyProcess::Terminate()
	{
	}
} // namespace DefectStudio::Platform
#endif
