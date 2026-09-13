#pragma once

// Shared by ProcessRunner.cpp (one-shot process, final result) and InteractiveProcess.cpp
// (long-lived process, streamed I/O) - both spawn a Win32 child via CreateProcessW over
// inheritable pipes and needed the same command-line quoting/pipe setup. Windows-only; each
// includer wraps its own #if defined(DS_PLATFORM_WINDOWS) around the #include, so this header
// assumes <windows.h> is already visible.

#include <cctype>
#include <sstream>
#include <string>
#include <vector>

#include "Core/Utils/Path.hpp"

namespace DefectStudio::Platform::Internal
{
	struct PipeHandles
	{
		HANDLE read = nullptr;
		HANDLE write = nullptr;
	};

	inline std::wstring ToWideString(const std::string &value)
	{
		if (value.empty())
			return {};
		const int requiredSize = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, nullptr, 0);
		if (requiredSize <= 0)
			return std::wstring(value.begin(), value.end());
		std::wstring output(static_cast<std::size_t>(requiredSize - 1), L'\0');
		MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, output.data(), requiredSize);
		return output;
	}

	inline std::string QuoteCommandArgument(const std::string &argument)
	{
		if (argument.empty())
			return "\"\"";

		bool needsQuoting = false;
		for (const char ch : argument)
		{
			if (std::isspace(static_cast<unsigned char>(ch)) || ch == '"')
			{
				needsQuoting = true;
				break;
			}
		}
		if (!needsQuoting)
			return argument;

		std::string quoted;
		quoted.reserve(argument.size() + 2);
		quoted.push_back('"');
		for (const char ch : argument)
		{
			if (ch == '"')
				quoted.push_back('\\');
			quoted.push_back(ch);
		}
		quoted.push_back('"');
		return quoted;
	}

	inline std::string BuildCommandLine(const Path &executable, const std::vector<std::string> &arguments)
	{
		std::ostringstream command;
		command << QuoteCommandArgument(executable.String());
		for (const std::string &argument : arguments)
			command << ' ' << QuoteCommandArgument(argument);
		return command.str();
	}

	// Every child we spawn gets its own job object with KILL_ON_JOB_CLOSE, and job membership is
	// inherited by anything that child spawns. Closing the handle therefore kills the whole tree -
	// whether we close it deliberately in Terminate() or the OS closes it for us because
	// DefectStudio crashed or was killed from Task Manager. Both gaps are real: destructors cannot
	// run on a hard kill, and TerminateProcess only ever reaches the one handle we hold, so a venv
	// console script like ipython.exe (a launcher stub that re-spawns the real python.exe) left
	// that python.exe running forever. Returns nullptr if the job could not be created; callers
	// treat that as "no cleanup guarantee" and carry on rather than failing the spawn.
	inline HANDLE CreateKillOnCloseJob()
	{
		HANDLE job = CreateJobObjectW(nullptr, nullptr);
		if (job == nullptr)
			return nullptr;
		JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
		limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
		if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits)))
		{
			CloseHandle(job);
			return nullptr;
		}
		return job;
	}

	// Call right after a CreateProcessW that passed CREATE_SUSPENDED. Suspension is what closes the
	// race: an unsuspended launcher can spawn its grandchild before we manage to assign it, and
	// that grandchild would then be outside the job. The child is resumed even when assignment
	// failed - a child that escapes cleanup beats a child frozen forever.
	inline void AdoptChildAndResume(HANDLE job, const PROCESS_INFORMATION &processInfo)
	{
		if (job != nullptr)
			AssignProcessToJobObject(job, processInfo.hProcess);
		ResumeThread(processInfo.hThread);
	}

	// Both ends start inheritable; caller clears HANDLE_FLAG_INHERIT on whichever end it keeps
	// for itself (the other end is handed to the child and closed in the parent afterwards).
	inline bool CreateInheritablePipe(PipeHandles &pipe)
	{
		SECURITY_ATTRIBUTES attributes{};
		attributes.nLength = sizeof(SECURITY_ATTRIBUTES);
		attributes.bInheritHandle = TRUE;
		attributes.lpSecurityDescriptor = nullptr;
		return ::CreatePipe(&pipe.read, &pipe.write, &attributes, 0) != 0;
	}
} // namespace DefectStudio::Platform::Internal
