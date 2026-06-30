#pragma once

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "Core/Diagnostics/StructuredError.hpp"
#include "Core/Logging/Logger.hpp"
#include "Core/Utils/Path.hpp"

namespace DefectStudio
{
	class LogCsvIO final
	{
	public:
		LogCsvIO() = delete;

		[[nodiscard]] static std::string Serialize(std::span<const LogEntry> entries);
		[[nodiscard]] static Result<std::size_t> Save(const Path &path, std::span<const LogEntry> entries);
	};
} // namespace DefectStudio
