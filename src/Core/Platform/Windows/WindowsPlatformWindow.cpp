#include "Core/dspch.hpp"

#if defined(_WIN32)


#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include <dbghelp.h>
#include <dwmapi.h>
#include <corecrt.h>
#include <crtdbg.h>
#include <cstdlib>
#include <windows.h>

#include "Core/Utils/Logger.hpp"
#include "Core/Utils/Path.hpp"
#include "Core/Platform/PlatformPaths.hpp"
#include "Core/Platform/PlatformSystem.hpp"
#include "Core/Platform/PlatformWindow.hpp"

#pragma comment(lib, "Dbghelp.lib")

struct WindowIconState
{
	HICON icon = nullptr;
	HICON previousClassIcon = nullptr;
	HICON previousClassSmallIcon = nullptr;
};

static std::unordered_map<GLFWwindow *, WindowIconState> s_WindowIcons;

namespace
{
	constexpr wchar_t kWindowIconResourceName[] = L"IDI_ICON1";
	DefectStudio::Platform::NativeCrashCallback s_NativeCrashCallback = nullptr;

	std::string NarrowWideString(const wchar_t *text)
	{
		if (text == nullptr || text[0] == L'\0')
			return {};

		const int requiredSize = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
		if (requiredSize <= 1)
			return {};

		std::string converted(static_cast<std::size_t>(requiredSize - 1), '\0');
		WideCharToMultiByte(
			CP_UTF8,
			0,
			text,
			-1,
			converted.data(),
			requiredSize,
			nullptr,
			nullptr);
		return converted;
	}

	std::string BasenameFromPath(const std::string &path)
	{
		if (path.empty())
			return {};
		const std::size_t slash = path.find_last_of("/\\");
		if (slash == std::string::npos)
			return path;
		return path.substr(slash + 1);
	}

	DefectStudio::Path GetExecutableDirectory()
	{
		wchar_t modulePath[MAX_PATH] = {};
		const DWORD modulePathLength = GetModuleFileNameW(nullptr, modulePath, MAX_PATH);
		if (modulePathLength == 0 || modulePathLength >= MAX_PATH)
			return {};

		return DefectStudio::Path::FromResolved(FilePath(modulePath).parent_path());
	}

	DefectStudio::Path ResolveIconPath(const DefectStudio::Path &iconPath)
	{
		if (iconPath.Empty())
			return {};

		if (iconPath.Native().is_absolute() && FileSystem::Exists(iconPath.Native()))
			return DefectStudio::Path::FromResolved(iconPath.Native());

		const DefectStudio::Path fromCurrentDirectory = DefectStudio::Path::FromResolved(FileSystem::CurrentPath()) / iconPath;
		if (FileSystem::Exists(fromCurrentDirectory.Native()))
			return fromCurrentDirectory;

		const DefectStudio::Path executableDirectory = GetExecutableDirectory();
		if (!executableDirectory.Empty())
		{
			const DefectStudio::Path fromExecutableDirectory = executableDirectory / iconPath;
			if (FileSystem::Exists(fromExecutableDirectory.Native()))
				return fromExecutableDirectory;
		}

		return {};
	}

	HICON LoadWindowIconFromResource()
	{
		const HINSTANCE moduleHandle = GetModuleHandleW(nullptr);
		if (moduleHandle == nullptr)
			return nullptr;

		return static_cast<HICON>(LoadImageW(moduleHandle, kWindowIconResourceName, IMAGE_ICON, 0, 0, LR_DEFAULTSIZE));
	}

	HICON LoadWindowIconFromFile(const DefectStudio::Path &iconPath)
	{
		const DefectStudio::Path resolvedIconPath = ResolveIconPath(iconPath);
		if (resolvedIconPath.Empty())
			return nullptr;

		return static_cast<HICON>(LoadImageW(nullptr,
		                                     resolvedIconPath.wstring().c_str(),
		                                     IMAGE_ICON,
		                                     0,
		                                     0,
		                                     LR_DEFAULTSIZE | LR_LOADFROMFILE));
	}

	WindowIconState ApplyWindowIcon(HWND hwnd, HICON icon)
	{
		WindowIconState state;
		if (hwnd == nullptr || icon == nullptr)
			return state;

		state.icon = icon;
		state.previousClassIcon = reinterpret_cast<HICON>(SetClassLongPtrW(hwnd, GCLP_HICON, reinterpret_cast<LONG_PTR>(icon)));
		state.previousClassSmallIcon = reinterpret_cast<HICON>(SetClassLongPtrW(hwnd, GCLP_HICONSM, reinterpret_cast<LONG_PTR>(icon)));
		SendMessageW(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(icon));
		SendMessageW(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(icon));
		return state;
	}

	std::optional<FilePath> EnvironmentDirectory(const char *variableName, const FilePath &suffix = {})
	{
		char *buffer = nullptr;
		std::size_t bufferSize = 0;
		if (_dupenv_s(&buffer, &bufferSize, variableName) != 0 || buffer == nullptr)
			return std::nullopt;

		std::string value(buffer);
		std::free(buffer);
		if (value.empty())
			return std::nullopt;

		FilePath directory(value);
		if (!suffix.empty())
			directory /= suffix;
		return directory;
	}

	void AppendEnvironmentDirectory(std::vector<FilePath> &directories, const char *variableName, const FilePath &suffix = {})
	{
		if (auto directory = EnvironmentDirectory(variableName, suffix))
			directories.push_back(std::move(*directory));
	}

	LONG WINAPI WindowsUnhandledExceptionFilter(EXCEPTION_POINTERS *exceptionPointers)
	{
		const unsigned long code = exceptionPointers != nullptr && exceptionPointers->ExceptionRecord != nullptr
			? exceptionPointers->ExceptionRecord->ExceptionCode
			: 0UL;
		const unsigned long flags = exceptionPointers != nullptr && exceptionPointers->ExceptionRecord != nullptr
			? exceptionPointers->ExceptionRecord->ExceptionFlags
			: 0UL;
		void *address = exceptionPointers != nullptr && exceptionPointers->ExceptionRecord != nullptr
			? exceptionPointers->ExceptionRecord->ExceptionAddress
			: nullptr;

		DefectStudio::Logger::Flush();
		char buffer[256] = {};
		std::snprintf(buffer,
		              sizeof(buffer),
		              "[CRASH] unhandled SEH exception code=0x%08lX flags=0x%08lX address=%p",
		              code,
		              flags,
		              address);
		if (s_NativeCrashCallback != nullptr)
			s_NativeCrashCallback(buffer);
		return EXCEPTION_EXECUTE_HANDLER;
	}

	void WindowsInvalidParameterHandler(
		const wchar_t *expression,
		const wchar_t *functionName,
		const wchar_t *fileName,
		unsigned int lineNumber,
		uintptr_t reserved)
	{
		(void)reserved;
		DefectStudio::Logger::Flush();
		const std::string expressionUtf8 = NarrowWideString(expression);
		const std::string functionUtf8 = NarrowWideString(functionName);
		const std::string fileUtf8 = NarrowWideString(fileName);

		char buffer[512] = {};
		std::snprintf(
			buffer,
			sizeof(buffer),
			"[CRASH] invalid parameter expression='%s' function='%s' file='%s' line=%u",
			expressionUtf8.c_str(),
			functionUtf8.c_str(),
			fileUtf8.c_str(),
			lineNumber);
		if (s_NativeCrashCallback != nullptr)
			s_NativeCrashCallback(buffer);

		std::abort();
	}

	void WindowsPurecallHandler()
	{
		DefectStudio::Logger::Flush();
		if (s_NativeCrashCallback != nullptr)
			s_NativeCrashCallback("[CRASH] pure virtual function call");
		std::abort();
	}

	int WindowsCrtReportHook(int reportType, wchar_t *message, int *returnValue)
	{
		(void)returnValue;
		if (s_NativeCrashCallback == nullptr || message == nullptr)
			return FALSE;

		const std::string messageUtf8 = NarrowWideString(message);
		char buffer[1024] = {};
		std::snprintf(
			buffer,
			sizeof(buffer),
			"[CRASH] CRT report type=%d message=%s",
			reportType,
			messageUtf8.c_str());
		s_NativeCrashCallback(buffer);
		return FALSE;
	}
}

namespace DefectStudio::Platform
{
	void DebugBreak()
	{
		::DebugBreak();
	}

	void InstallNativeCrashHandler(NativeCrashCallback callback)
	{
		s_NativeCrashCallback = callback;
		_set_invalid_parameter_handler(WindowsInvalidParameterHandler);
		_set_purecall_handler(WindowsPurecallHandler);
		_CrtSetReportHookW2(_CRT_RPTHOOK_INSTALL, WindowsCrtReportHook);
		SetUnhandledExceptionFilter(WindowsUnhandledExceptionFilter);
	}

	void AppendNativeCrashStackTrace(NativeCrashCallback callback, unsigned int framesToSkip)
	{
		if (callback == nullptr)
			return;

		const HANDLE processHandle = GetCurrentProcess();
		SymSetOptions(SYMOPT_UNDNAME | SYMOPT_LOAD_LINES | SYMOPT_DEFERRED_LOADS);
		SymInitialize(processHandle, nullptr, TRUE);

		void *frames[48] = {};
		const USHORT capturedFrames = CaptureStackBackTrace(framesToSkip + 1, 48, frames, nullptr);
		for (USHORT frameIndex = 0; frameIndex < capturedFrames; ++frameIndex)
		{
			const DWORD64 address = reinterpret_cast<DWORD64>(frames[frameIndex]);
			char symbolStorage[sizeof(SYMBOL_INFO) + MAX_SYM_NAME] = {};
			SYMBOL_INFO *symbol = reinterpret_cast<SYMBOL_INFO *>(symbolStorage);
			symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
			symbol->MaxNameLen = MAX_SYM_NAME;
			DWORD64 symbolDisplacement = 0;

			std::string symbolName = "unknown";
			if (SymFromAddr(processHandle, address, &symbolDisplacement, symbol))
				symbolName = symbol->Name;

			IMAGEHLP_LINE64 sourceLine = {};
			sourceLine.SizeOfStruct = sizeof(IMAGEHLP_LINE64);
			DWORD sourceDisplacement = 0;
			std::string sourceFile;
			DWORD sourceLineNumber = 0;
			if (SymGetLineFromAddr64(processHandle, address, &sourceDisplacement, &sourceLine))
			{
				sourceFile = BasenameFromPath(sourceLine.FileName != nullptr ? sourceLine.FileName : "");
				sourceLineNumber = sourceLine.LineNumber;
			}

			HMODULE moduleHandle = nullptr;
			std::string moduleName = "unknown";
			unsigned long long moduleOffset = 0ULL;
			if (GetModuleHandleExW(
				    GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
				    reinterpret_cast<LPCWSTR>(address),
				    &moduleHandle) != 0
			    && moduleHandle != nullptr)
			{
				wchar_t modulePath[MAX_PATH] = {};
				if (GetModuleFileNameW(moduleHandle, modulePath, MAX_PATH) > 0)
					moduleName = BasenameFromPath(NarrowWideString(modulePath));
				const auto moduleBase = reinterpret_cast<DWORD64>(moduleHandle);
				moduleOffset = address >= moduleBase ? static_cast<unsigned long long>(address - moduleBase) : 0ULL;
			}

			char line[512] = {};
			if (sourceLineNumber > 0 && !sourceFile.empty())
			{
				std::snprintf(
					line,
					sizeof(line),
					"[CRASH] stack[%u]=%p module=%s+0x%llX symbol=%s+0x%llX source=%s:%lu",
					static_cast<unsigned int>(frameIndex),
					reinterpret_cast<void *>(address),
					moduleName.c_str(),
					moduleOffset,
					symbolName.c_str(),
					static_cast<unsigned long long>(symbolDisplacement),
					sourceFile.c_str(),
					static_cast<unsigned long>(sourceLineNumber));
			}
			else
			{
				std::snprintf(
					line,
					sizeof(line),
					"[CRASH] stack[%u]=%p module=%s+0x%llX symbol=%s+0x%llX",
					static_cast<unsigned int>(frameIndex),
					reinterpret_cast<void *>(address),
					moduleName.c_str(),
					moduleOffset,
					symbolName.c_str(),
					static_cast<unsigned long long>(symbolDisplacement));
			}
			callback(line);
		}
	}

	bool LocalTime(std::time_t time, std::tm &outLocalTime)
	{
		return localtime_s(&outLocalTime, &time) == 0;
	}

	std::vector<FilePath> GetSystemFontDirectories()
	{
		std::vector<FilePath> directories;
		AppendEnvironmentDirectory(directories, "WINDIR", "Fonts");
		AppendEnvironmentDirectory(directories, "SystemRoot", "Fonts");
		directories.emplace_back("C:/Windows/Fonts");
		return directories;
	}

	void InitializeWindowPlatform(GLFWwindow *window, const Path &iconPath)
	{
		if (window == nullptr)
			return;

		const HWND hwnd = glfwGetWin32Window(window);
		if (hwnd == nullptr)
			return;

		BOOL useDark = TRUE;
		if (DwmSetWindowAttribute(hwnd, 20, &useDark, sizeof(useDark)) != S_OK)
			DwmSetWindowAttribute(hwnd, 19, &useDark, sizeof(useDark));

		HICON icon = LoadWindowIconFromResource();
		if (icon == nullptr)
			icon = LoadWindowIconFromFile(iconPath);
		if (icon == nullptr)
		{
			DS_LOG_WARN("Failed to assign a window icon");
			return;
		}

		s_WindowIcons[window] = ApplyWindowIcon(hwnd, icon);
		DS_LOG_INFO("Windows icon assigned");
	}

	void ShutdownWindowPlatform(GLFWwindow *window)
	{
		if (window == nullptr)
			return;

		auto iconIt = s_WindowIcons.find(window);
		if (iconIt == s_WindowIcons.end())
			return;

		const HWND hwnd = glfwGetWin32Window(window);
		if (hwnd != nullptr)
		{
			SetClassLongPtrW(hwnd, GCLP_HICON, reinterpret_cast<LONG_PTR>(iconIt->second.previousClassIcon));
			SetClassLongPtrW(hwnd, GCLP_HICONSM, reinterpret_cast<LONG_PTR>(iconIt->second.previousClassSmallIcon));
			SendMessageW(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(iconIt->second.previousClassIcon));
			SendMessageW(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(iconIt->second.previousClassSmallIcon));
		}

		DestroyIcon(iconIt->second.icon);
		s_WindowIcons.erase(iconIt);
	}
} // namespace DefectStudio::Platform

#endif
