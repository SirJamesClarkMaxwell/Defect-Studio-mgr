#pragma once

#include "ScientificRuntime/Python/PhononAnalysisBridge.hpp"

namespace DefectStudio
{
	struct ServerJobRequest
	{
		PhononAnalysisRequest inputs;
		Path templatesDirectory;
		Path fallbackOutputDirectory; // only for an automatic default, never for an explicit destination
		int nMin = 10;
		int nMax = 50;
	};

	[[nodiscard]] std::vector<std::string> BuildServerJobArguments(const ServerJobRequest &request);
	[[nodiscard]] Result<Path> ParseServerJobOutput(const std::string &standardOutput);

	class ServerJobBridge final
	{
	public:
		[[nodiscard]] Result<Path> Run(const ServerJobRequest &request, std::function<bool()> shouldCancel = {}) const;
	private:
		ScriptRunner m_ScriptRunner;
	};

	class ServerJob final : public IJob
	{
	public:
		explicit ServerJob(ServerJobRequest request);
		[[nodiscard]] std::string GetName() const override;
		[[nodiscard]] std::string GetType() const override;
		void Execute(JobContext &context) override;
		[[nodiscard]] const std::optional<Path> &GetResult() const noexcept { return m_Result; }
		[[nodiscard]] const std::optional<StructuredError> &GetError() const noexcept { return m_Error; }
	private:
		ServerJobRequest m_Request;
		ServerJobBridge m_Bridge;
		std::optional<Path> m_Result;
		std::optional<StructuredError> m_Error;
	};
}
