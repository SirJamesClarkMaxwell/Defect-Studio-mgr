#pragma once

#include <string>

#include "Core/Utils/Path.hpp"

namespace DefectStudio::Platform
{
	[[nodiscard]] Path GetAppPythonRoot();
	[[nodiscard]] Path GetAppPythonPlatformRoot();
	[[nodiscard]] Path ResolvePreferredPythonExecutable();
	// The dev `.venv` specifically, never the redistributable app-local runtime
	// (install/app/python) - dev-only tools like ipython are installed there and nowhere else.
	// See docs/work/project/plans/2026-08-24-calc-tools.md section 4.
	[[nodiscard]] Path ResolveVenvPythonExecutable();
	[[nodiscard]] Path ResolveEmbeddedPythonHome();
	void SetPythonHomeEnvironment(const Path &pythonHome);
} // namespace DefectStudio::Platform
