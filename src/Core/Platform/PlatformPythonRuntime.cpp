#include "Core/dspch.hpp"

#include <cstdlib>

#include "Core/Platform/PlatformPythonRuntime.hpp"

namespace DefectStudio::Platform
{
	static Path resolveExistingFromCurrentOrAncestor(const Path &relativePath)
	{
		if (relativePath.Empty() || relativePath.Native().is_absolute())
			return relativePath;

		Path cursor = Path::FromResolved(FileSystem::CurrentPath());
		for (int depth = 0; depth < 10; ++depth)
		{
			const Path candidate = cursor / relativePath;
			if (FileSystem::Exists(candidate.Native()))
				return candidate;

			const Path parent = cursor.parent_path();
			if (parent.Empty() || parent == cursor)
				break;
			cursor = parent;
		}

		return relativePath;
	}

	static Path getAppPythonPlatformExecutable()
	{
#if defined(DS_PLATFORM_WINDOWS)
		return GetAppPythonPlatformRoot() / "python.exe";
#else
		return GetAppPythonPlatformRoot() / "bin" / "python3";
#endif
	}

	static Path getAppPythonGenericExecutable()
	{
#if defined(DS_PLATFORM_WINDOWS)
		return GetAppPythonRoot() / "python.exe";
#else
		return GetAppPythonRoot() / "bin" / "python3";
#endif
	}

	static Path getVenvPythonExecutable()
	{
#if defined(DS_PLATFORM_WINDOWS)
		return resolveExistingFromCurrentOrAncestor(Path(".venv") / "Scripts" / "python.exe");
#else
		return resolveExistingFromCurrentOrAncestor(Path(".venv") / "bin" / "python");
#endif
	}

	Path GetAppPythonRoot()
	{
		return resolveExistingFromCurrentOrAncestor(Path("install") / "app" / "python");
	}

	Path GetAppPythonPlatformRoot()
	{
#if defined(DS_PLATFORM_WINDOWS)
		return GetAppPythonRoot() / "windows";
#elif defined(DS_PLATFORM_LINUX)
		return GetAppPythonRoot() / "linux";
#elif defined(DS_PLATFORM_MACOS)
		return GetAppPythonRoot() / "macos";
#else
		return {};
#endif
	}

	Path ResolvePreferredPythonExecutable()
	{
		const Path platformPython = getAppPythonPlatformExecutable();
		if (FileSystem::Exists(platformPython.Native()))
			return platformPython;

		const Path genericPython = getAppPythonGenericExecutable();
		if (FileSystem::Exists(genericPython.Native()))
			return genericPython;

		const Path venvPython = getVenvPythonExecutable();
		if (FileSystem::Exists(venvPython.Native()))
			return venvPython;

		return Path("python");
	}

	Path ResolveEmbeddedPythonHome()
	{
		const Path platformRoot = GetAppPythonPlatformRoot();
		if (!platformRoot.Empty() && FileSystem::Exists(platformRoot.Native()))
			return platformRoot;

		const Path appRoot = GetAppPythonRoot();
		if (FileSystem::Exists(appRoot.Native()))
			return appRoot;

		return {};
	}

	void SetPythonHomeEnvironment(const Path &pythonHome)
	{
		if (pythonHome.Empty())
			return;

#if defined(DS_PLATFORM_WINDOWS)
		_wputenv_s(L"PYTHONHOME", pythonHome.wstring().c_str());
#else
		const std::string pythonHomeUtf8 = pythonHome.String();
		setenv("PYTHONHOME", pythonHomeUtf8.c_str(), 1);
#endif
	}
} // namespace DefectStudio::Platform
