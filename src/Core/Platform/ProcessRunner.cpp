#include "Core/dspch.hpp"

#include "Core/Platform/ProcessRunner.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <sstream>
#include <thread>

#if defined(DS_PLATFORM_WINDOWS)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace DefectStudio::Platform
{
	namespace
	{
		StructuredError MakeProcessError(std::string technicalDetails, std::string code)
		{
			return StructuredError{
				ErrorCategory::Runtime,
				Severity::Error,
				"Process execution failed.",
				std::move(technicalDetails),
				"Verify executable path, arguments and working directory.",
				"Core/Platform/ProcessRunner",
				std::move(code)};
		}

		std::string QuoteCommandArgument(const std::string &argument)
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

		std::string BuildCommandLine(const Path &executable, const std::vector<std::string> &arguments)
		{
			std::ostringstream command;
			command << QuoteCommandArgument(executable.String());
			for (const std::string &argument : arguments)
				command << ' ' << QuoteCommandArgument(argument);
			return command.str();
		}

		bool ShouldTerminateProcess(
			const ProcessRunOptions &options,
			const std::chrono::steady_clock::time_point &startedAt,
			ProcessRunResult &result)
		{
			if (options.shouldCancel && options.shouldCancel())
			{
				result.cancelled = true;
				result.terminated = true;
				return true;
			}

			if (options.timeout.count() > 0 &&
				std::chrono::steady_clock::now() - startedAt >= options.timeout)
			{
				result.timedOut = true;
				result.terminated = true;
				return true;
			}

			return false;
		}

#if defined(DS_PLATFORM_WINDOWS)
		std::wstring ToWideString(const std::string &value)
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

		std::string ReadPipeUntilClosed(HANDLE readHandle)
		{
			std::string output;
			char buffer[4096];
			DWORD bytesRead = 0;
			while (ReadFile(readHandle, buffer, static_cast<DWORD>(sizeof(buffer)), &bytesRead, nullptr) && bytesRead > 0)
				output.append(buffer, buffer + bytesRead);
			return output;
		}

		struct PipeHandles
		{
			HANDLE read = nullptr;
			HANDLE write = nullptr;
		};

		bool CreateInheritablePipe(PipeHandles &pipe)
		{
			SECURITY_ATTRIBUTES attributes{};
			attributes.nLength = sizeof(SECURITY_ATTRIBUTES);
			attributes.bInheritHandle = TRUE;
			attributes.lpSecurityDescriptor = nullptr;
			if (!CreatePipe(&pipe.read, &pipe.write, &attributes, 0))
				return false;
			if (!SetHandleInformation(pipe.read, HANDLE_FLAG_INHERIT, 0))
				return false;
			return true;
		}
#else
		bool ReadAvailableFromFd(int fd, std::string &output)
		{
			char buffer[4096];
			for (;;)
			{
				const ssize_t bytesRead = read(fd, buffer, sizeof(buffer));
				if (bytesRead > 0)
				{
					output.append(buffer, buffer + bytesRead);
					continue;
				}
				if (bytesRead == 0)
					return false;
				if (errno == EAGAIN || errno == EWOULDBLOCK)
					return true;
				return false;
			}
		}
#endif
	} // namespace

	Result<ProcessRunResult> RunProcess(const ProcessRunOptions &options)
	{
		if (options.executable.Empty())
			return MakeProcessError("Executable path is empty.", "process.executable_missing");

		ProcessRunResult result;
		result.commandLine = BuildCommandLine(options.executable, options.arguments);

#if defined(DS_PLATFORM_WINDOWS)
		PipeHandles stdoutPipe;
		PipeHandles stderrPipe;
		if (!CreateInheritablePipe(stdoutPipe) || !CreateInheritablePipe(stderrPipe))
			return MakeProcessError("CreatePipe failed.", "process.pipe_failed");

		STARTUPINFOW startupInfo{};
		startupInfo.cb = sizeof(STARTUPINFOW);
		startupInfo.dwFlags = STARTF_USESTDHANDLES;
		startupInfo.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
		startupInfo.hStdOutput = stdoutPipe.write;
		startupInfo.hStdError = stderrPipe.write;

		PROCESS_INFORMATION processInfo{};
		std::wstring commandLine = ToWideString(result.commandLine);
		const std::wstring workingDirectory = options.workingDirectory.Empty() ? std::wstring{} : options.workingDirectory.wstring();
		BOOL created = CreateProcessW(
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

		CloseHandle(stdoutPipe.write);
		CloseHandle(stderrPipe.write);

		if (!created)
		{
			CloseHandle(stdoutPipe.read);
			CloseHandle(stderrPipe.read);
			return MakeProcessError("CreateProcessW failed with code " + std::to_string(GetLastError()), "process.create_failed");
		}

		std::thread stdoutReader([&]() { result.standardOutput = ReadPipeUntilClosed(stdoutPipe.read); });
		std::thread stderrReader([&]() { result.standardError = ReadPipeUntilClosed(stderrPipe.read); });

		const auto startedAt = std::chrono::steady_clock::now();
		for (;;)
		{
			const DWORD waitResult = WaitForSingleObject(processInfo.hProcess, 50);
			if (waitResult == WAIT_OBJECT_0)
				break;
			if (waitResult != WAIT_TIMEOUT)
				break;
			if (ShouldTerminateProcess(options, startedAt, result))
			{
				TerminateProcess(processInfo.hProcess, 1);
				WaitForSingleObject(processInfo.hProcess, INFINITE);
				break;
			}
		}
		DWORD exitCode = 0;
		GetExitCodeProcess(processInfo.hProcess, &exitCode);
		result.exitCode = result.terminated ? -1 : static_cast<int>(exitCode);

		stdoutReader.join();
		stderrReader.join();
		CloseHandle(stdoutPipe.read);
		CloseHandle(stderrPipe.read);
		CloseHandle(processInfo.hThread);
		CloseHandle(processInfo.hProcess);
#else
		int stdoutPipe[2]{};
		int stderrPipe[2]{};
		if (pipe(stdoutPipe) != 0 || pipe(stderrPipe) != 0)
			return MakeProcessError(std::string("pipe failed: ") + std::strerror(errno), "process.pipe_failed");

		std::vector<std::string> storage;
		storage.reserve(options.arguments.size() + 1);
		storage.push_back(options.executable.String());
		storage.insert(storage.end(), options.arguments.begin(), options.arguments.end());
		std::vector<char *> argv;
		argv.reserve(storage.size() + 1);
		for (std::string &item : storage)
			argv.push_back(item.data());
		argv.push_back(nullptr);

		pid_t pid = fork();
		if (pid < 0)
		{
			close(stdoutPipe[0]);
			close(stdoutPipe[1]);
			close(stderrPipe[0]);
			close(stderrPipe[1]);
			return MakeProcessError(std::string("fork failed: ") + std::strerror(errno), "process.create_failed");
		}
		if (pid == 0)
		{
			close(stdoutPipe[0]);
			close(stderrPipe[0]);
			dup2(stdoutPipe[1], STDOUT_FILENO);
			dup2(stderrPipe[1], STDERR_FILENO);
			close(stdoutPipe[1]);
			close(stderrPipe[1]);
			if (!options.workingDirectory.Empty() && chdir(options.workingDirectory.String().c_str()) != 0)
				_exit(126);
			execvp(options.executable.String().c_str(), argv.data());
			_exit(127);
		}

		close(stdoutPipe[1]);
		close(stderrPipe[1]);

		fcntl(stdoutPipe[0], F_SETFL, O_NONBLOCK);
		fcntl(stderrPipe[0], F_SETFL, O_NONBLOCK);
		bool stdoutOpen = true;
		bool stderrOpen = true;
		bool childRunning = true;
		int status = 0;
		const auto startedAt = std::chrono::steady_clock::now();
		while (stdoutOpen || stderrOpen || childRunning)
		{
			if (childRunning)
			{
				const pid_t waitResult = waitpid(pid, &status, WNOHANG);
				if (waitResult == pid)
					childRunning = false;
			}
			if (childRunning && ShouldTerminateProcess(options, startedAt, result))
			{
				kill(pid, SIGKILL);
				waitpid(pid, &status, 0);
				childRunning = false;
			}

			fd_set set;
			FD_ZERO(&set);
			int maxFd = -1;
			if (stdoutOpen)
			{
				FD_SET(stdoutPipe[0], &set);
				maxFd = std::max(maxFd, stdoutPipe[0]);
			}
			if (stderrOpen)
			{
				FD_SET(stderrPipe[0], &set);
				maxFd = std::max(maxFd, stderrPipe[0]);
			}
			if (maxFd < 0)
				continue;

			timeval timeout{};
			timeout.tv_sec = 0;
			timeout.tv_usec = 50000;
			if (select(maxFd + 1, &set, nullptr, nullptr, &timeout) <= 0)
				continue;
			if (stdoutOpen && FD_ISSET(stdoutPipe[0], &set))
				stdoutOpen = ReadAvailableFromFd(stdoutPipe[0], result.standardOutput);
			if (stderrOpen && FD_ISSET(stderrPipe[0], &set))
				stderrOpen = ReadAvailableFromFd(stderrPipe[0], result.standardError);
		}
		close(stdoutPipe[0]);
		close(stderrPipe[0]);

		if (result.terminated)
			result.exitCode = -1;
		else if (WIFEXITED(status))
			result.exitCode = WEXITSTATUS(status);
		else
			result.exitCode = -1;
#endif
		return result;
	}
} // namespace DefectStudio::Platform
