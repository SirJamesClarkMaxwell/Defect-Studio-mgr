#pragma once

#include "Core/JobSystem/JobSystemTypes.hpp"
#include <optional>
#include "ScientificRuntime/Python/ScriptRunner.hpp"

namespace DefectStudio
{
	class PhononModeVectorJob final : public IJob
	{
	public:
		PhononModeVectorJob(Path embeddedDirectory, Path outputDirectory, std::size_t mode);
		~PhononModeVectorJob() override;
		[[nodiscard]] std::string GetName() const override;
		[[nodiscard]] std::string GetType() const override;
		void Execute(JobContext &context) override;
		[[nodiscard]] const std::optional<Path> &GetResult() const noexcept { return m_Result; }
		[[nodiscard]] const std::optional<StructuredError> &GetError() const noexcept { return m_Error; }

	private:
		Path m_EmbeddedDirectory, m_OutputDirectory;
		FilePath m_OwnedOutput;
		std::size_t m_Mode;
		ScriptRunner m_ScriptRunner;
		std::optional<Path> m_Result;
		std::optional<StructuredError> m_Error;
	};
}
