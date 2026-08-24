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
