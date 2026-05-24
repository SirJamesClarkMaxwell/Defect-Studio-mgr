#pragma once

#include <string>

#include "Core/Diagnostics/StructuredError.hpp"

namespace DefectStudio
{
	[[nodiscard]] StructuredError MakePythonUnavailableError(
		const std::string &userMessage,
		const std::string &technicalDetails,
		const std::string &suggestion,
		const std::string &code);

	[[nodiscard]] StructuredError MakePythonExecutionError(
		const std::string &userMessage,
		const std::string &technicalDetails,
		const std::string &suggestion,
		const std::string &code);
} // namespace DefectStudio
