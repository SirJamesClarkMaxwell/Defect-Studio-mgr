#pragma once

#include <string>
#include "Core/Utils/Path.hpp"
#include "Core/Diagnostics/StructuredError.hpp"
#include "ScientificRuntime/Python/ScriptRunner.hpp"

namespace DefectStudio
{
	struct PymatgenRoundtripRequest
	{
		Path inputPoscarPath;
		Path outputPoscarPath;
	};

	struct PymatgenRoundtripResult
	{
		Path outputPoscarPath;
		std::string reducedFormula;
		int siteCount = 0;
	};

	class PymatgenBridge final
	{
	public:
		[[nodiscard]] Result<PymatgenRoundtripResult> RoundtripPoscar(const PymatgenRoundtripRequest &request) const;

	private:
		ScriptRunner m_ScriptRunner;
	};
} // namespace DefectStudio
