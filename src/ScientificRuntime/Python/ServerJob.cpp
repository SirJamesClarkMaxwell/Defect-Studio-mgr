#include "Core/dspch.hpp"
#include "ScientificRuntime/Python/ServerJobBridge.hpp"

#include <stdexcept>
#include "Core/JobSystem/JobContext.hpp"

namespace DefectStudio
{
	ServerJob::ServerJob(ServerJobRequest request) : m_Request(std::move(request)) {}
	std::string ServerJob::GetName() const { return "Prepare cluster job folder"; }
	std::string ServerJob::GetType() const { return "phonon.server_job"; }

	void ServerJob::Execute(JobContext &context)
	{
		context.ThrowIfCancellationRequested();
		m_Result.reset();
		m_Error.reset();
		context.SetStage("server_job");
		context.SetMessage("Preparing cluster scripts and aligned GS/ES");
		context.SetProgress(0.0f, 1.0f);
		auto result = m_Bridge.Run(m_Request, [&context] { return context.IsCancellationRequested(); });
		context.ThrowIfCancellationRequested();
		if (!result)
		{
			m_Error = result.Error();
			throw std::runtime_error(m_Error->userMessage + "\n" + m_Error->technicalDetails);
		}
		m_Result = std::move(result).Value();
		context.SetProgress(1.0f, 1.0f);
		context.SetMessage("Cluster job folder ready; submit manually");
	}
}
