#pragma once

#include <string>
#include <vector>

#include "Core/JobSystem/JobSystemTypes.hpp"
#include "Core/Utils/Path.hpp"
#include "ScientificRuntime/Python/ScriptRunner.hpp"

namespace DefectStudio
{
	class PythonScriptJob final : public IJob
	{
	public:
		PythonScriptJob(
			std::string name,
			Path scriptPath,
			std::vector<std::string> arguments = {},
			Path workingDirectory = {});

		[[nodiscard]] std::string GetName() const override;
		[[nodiscard]] std::string GetType() const override;
		[[nodiscard]] bool UsesPythonRuntime() const override;
		void Execute(JobContext &context) override;

	private:
		std::string m_Name;
		Path m_ScriptPath;
		std::vector<std::string> m_Arguments;
		Path m_WorkingDirectory;
		ScriptRunner m_ScriptRunner;
	};
} // namespace DefectStudio
