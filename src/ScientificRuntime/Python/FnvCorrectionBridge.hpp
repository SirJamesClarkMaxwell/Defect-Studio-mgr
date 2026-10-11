#pragma once

#include <optional>
#include <string>

#include <glm/glm.hpp>

#include "Core/JobSystem/JobSystemTypes.hpp"
#include "Core/Utils/Path.hpp"
#include "Domain/Defects/FnvCorrection.hpp"
#include "ScientificRuntime/Python/ScriptRunner.hpp"

namespace DefectStudio
{
	// task/86: step 1 of the FNV correction (Domain/Defects/FnvCorrection.hpp) through
	// pymatgen-analysis-defects (scripts/python/examples/fnv_freysoldt_profiles.py): reads the two
	// LOCPOTs, returns E_lat and the three planar-average profiles. Two 180^3 LOCPOT reads take about
	// a minute - call it from FnvCorrectionJob.
	struct FnvProfileRequest
	{
		Path defectLocpot;
		Path bulkLocpot;
		int charge = 0;                 // VASP charge state q (not sxdefectalign's --charge)
		double dielectric = 1.0;
		glm::dvec3 centreFractional = glm::dvec3(0.0);
		// eV. 0 = ENCUT from the INCAR next to the defect LOCPOT, and 520 eV when there is none.
		double energyCutoff = 0.0;
	};

	class FnvCorrectionBridge final
	{
	public:
		// Errors: a missing LOCPOT, LOCPOTs on different grids or cells, q = 0 (nothing to correct),
		// dielectric <= 0, pymatgen-analysis-defects not installed (python.pymatgen_defects.not_installed).
		[[nodiscard]] Result<FnvProfiles> ComputeProfiles(const FnvProfileRequest &request) const;

	private:
		ScriptRunner m_ScriptRunner;
	};

	// FnvCorrectionBridge off the main thread, same convention as VaspDensityGridJob.
	class FnvCorrectionJob final : public IJob
	{
	public:
		explicit FnvCorrectionJob(FnvProfileRequest request);

		[[nodiscard]] std::string GetName() const override;
		[[nodiscard]] std::string GetType() const override;
		void Execute(JobContext &context) override;

		[[nodiscard]] std::optional<FnvProfiles> &GetResult() noexcept;

	private:
		FnvProfileRequest m_Request;
		FnvCorrectionBridge m_Bridge;
		std::optional<FnvProfiles> m_Result;
	};
} // namespace DefectStudio
