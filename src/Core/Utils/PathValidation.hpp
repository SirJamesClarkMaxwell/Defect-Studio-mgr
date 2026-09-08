#pragma once

#include <string>
#include <string_view>

#include "Core/Diagnostics/StructuredError.hpp"
#include "Core/Utils/Path.hpp"

namespace DefectStudio::PathValidation
{
	// Generic, project-agnostic path checks. This module deliberately knows NOTHING about project
	// roots - deciding whether a directory belongs to a registered project is authorization, and
	// lives in App/StructureLifecycleCoordinator, the only layer that knows the set of roots.

	// Validates a single user-supplied directory-component name. Sanitization is limited to trimming
	// surrounding whitespace: anything else that would need rewriting is rejected instead, because
	// silently mangling a name is how two different user inputs end up colliding on disk.
	[[nodiscard]] Result<std::string> ValidateAndSanitizeName(std::string_view userInput);

	// Component-wise ancestor test, not a string prefix test: "C:\project-other" is never a child of
	// "C:\project". Both arguments must already be absolute and canonical - this function does no
	// resolution of its own. Comparison is case-insensitive on Windows (NTFS semantics).
	[[nodiscard]] bool IsAncestor(const FilePath &canonicalAncestor, const FilePath &canonicalPath);

	// Walks the path component by component and rejects if ANY existing component is a symlink or
	// (Windows) a junction/reparse point. This must run BEFORE canonical(), which resolves symlinks
	// rather than failing on them, and would therefore hand back a path that looks clean.
	// Components that do not exist yet are not followable and are skipped.
	[[nodiscard]] Result<void> RejectSymlinkComponents(const FilePath &path);
} // namespace DefectStudio::PathValidation
