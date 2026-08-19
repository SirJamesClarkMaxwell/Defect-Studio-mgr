#include "Core/dspch.hpp"

#include "ScientificRuntime/Python/OpenDefectJob.hpp"

#include "Core/JobSystem/JobContext.hpp"

namespace DefectStudio
{
	OpenDefectJob::OpenDefectJob(Path filePath, std::string displayName)
		: m_FilePath(std::move(filePath)), m_DisplayName(std::move(displayName))
	{
	}

	std::string OpenDefectJob::GetName() const
	{
		return "Open Defect: " + m_DisplayName;
	}

	std::string OpenDefectJob::GetType() const
	{
		return "OpenDefectJob";
	}

	void OpenDefectJob::Execute(JobContext &context)
	{
		context.SetStage("open-defect");
		context.SetMessage("Loading structure via puntukas");
		context.SetProgress(0.0f, 1.0f);

		Result<PymatgenStructureData> loadResult = m_Bridge.LoadStructure(m_FilePath);
		if (!loadResult)
			throw std::runtime_error(loadResult.Error().technicalDetails);

		m_Result = std::move(loadResult).Value();
		context.SetProgress(1.0f, 1.0f);
		context.SetMessage("Structure loaded");
	}

	const Path &OpenDefectJob::GetFilePath() const noexcept
	{
		return m_FilePath;
	}

	const std::string &OpenDefectJob::GetDisplayName() const noexcept
	{
		return m_DisplayName;
	}

	const std::optional<PymatgenStructureData> &OpenDefectJob::GetResult() const noexcept
	{
		return m_Result;
	}
} // namespace DefectStudio
