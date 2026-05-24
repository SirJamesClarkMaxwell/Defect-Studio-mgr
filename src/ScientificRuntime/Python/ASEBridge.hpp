#pragma once

#include <string>

#include "Core/Diagnostics/StructuredError.hpp"
#include "Core/Utils/Path.hpp"
#include "ScientificRuntime/Python/ScriptRunner.hpp"

namespace DefectStudio
{
	struct ASEConvertRequest
	{
		Path inputPath;
		Path outputPath;
		std::string inputFormat;
		std::string outputFormat;
	};

	struct ASEConvertResult
	{
		Path outputPath;
		std::string chemicalFormula;
		int atomCount = 0;
	};

	class ASEBridge final
	{
	public:
		[[nodiscard]] Result<ASEConvertResult> ConvertFile(const ASEConvertRequest &request) const;

	private:
		ScriptRunner m_ScriptRunner;
	};
} // namespace DefectStudio
