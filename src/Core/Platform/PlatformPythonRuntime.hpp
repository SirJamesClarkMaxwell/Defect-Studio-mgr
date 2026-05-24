#pragma once

#include <string>

#include "Core/Utils/Path.hpp"

namespace DefectStudio::Platform
{
	[[nodiscard]] Path GetAppPythonRoot();
	[[nodiscard]] Path GetAppPythonPlatformRoot();
	[[nodiscard]] Path ResolvePreferredPythonExecutable();
	[[nodiscard]] Path ResolveEmbeddedPythonHome();
	[[nodiscard]] std::string BuildShellCommand(const std::string &workingDirectorySegment, const std::string &payloadCommand);
	void SetPythonHomeEnvironment(const Path &pythonHome);
} // namespace DefectStudio::Platform
