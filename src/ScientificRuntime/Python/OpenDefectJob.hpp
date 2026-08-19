#pragma once

#include <optional>
#include <string>

#include "Core/JobSystem/JobSystemTypes.hpp"
#include "Core/Utils/Path.hpp"
#include "ScientificRuntime/Python/PuntukasBridge.hpp"

namespace DefectStudio
{
	// Loads a CONTCAR/POSCAR via PuntukasBridge off the main thread. On success the parsed structure
	// is available via GetResult() once JobCompletedEvent fires for this job's id (Execute() has fully
	// returned by then, so no synchronization is needed to read it back on the main thread). On
	// failure Execute() throws (same convention as PythonScriptJob), producing JobFailedEvent instead.
	class OpenDefectJob final : public IJob
	{
	public:
		OpenDefectJob(Path filePath, std::string displayName);

		[[nodiscard]] std::string GetName() const override;
		[[nodiscard]] std::string GetType() const override;
		void Execute(JobContext &context) override;

		[[nodiscard]] const Path &GetFilePath() const noexcept;
		[[nodiscard]] const std::string &GetDisplayName() const noexcept;
		[[nodiscard]] const std::optional<PymatgenStructureData> &GetResult() const noexcept;

	private:
		Path m_FilePath;
		std::string m_DisplayName;
		PuntukasBridge m_Bridge;
		std::optional<PymatgenStructureData> m_Result;
	};
} // namespace DefectStudio
