#pragma once

#include <string>

#include "Core/Utils/Path.hpp"

namespace DefectStudio::Platform
{
	[[nodiscard]] Path GetAppPythonRoot();
	[[nodiscard]] Path GetAppPythonPlatformRoot();
	[[nodiscard]] Path ResolvePreferredPythonExecutable();
	[[nodiscard]] Path ResolveEmbeddedPythonHome();
	void SetPythonHomeEnvironment(const Path &pythonHome);
} // namespace DefectStudio::Platform
