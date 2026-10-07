#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "Core/Utils/Memory.hpp"
#include "Presentation/Panels/IPanel.hpp"
#include "ScientificRuntime/Python/FnvCorrectionBridge.hpp"

namespace DefectStudio
{
	class JobSystem;
	class RendererLayer;

	class FnvCorrectionPanel final : public IPanel
	{
	public:
		explicit FnvCorrectionPanel(RendererLayer &rendererLayer, WeakRef<JobSystem> jobSystem,
			std::string title = "FNV Correction", bool visibleByDefault = false);

		void Render() override;
		[[nodiscard]] PanelCategory GetCategory() const override { return PanelCategory::Analysis; }
		[[nodiscard]] Ref<IPanel> Clone() const override;

	private:
		void drawData();
		void drawResult();
		void drawPlots();
		void drawAxisPlot(int axis);
		void invalidateProfiles();
		void submitProfiles();
		void pollJob();
		void proposeWindows();
		void copyResult() const;

		RendererLayer &m_RendererLayer;
		WeakRef<JobSystem> m_JobSystem;
		FnvProfileRequest m_Request;
		std::array<char, 256> m_DielectricSource{};
		Ref<FnvCorrectionJob> m_PendingJob;
		JobId m_PendingJobId = 0;
		std::uint64_t m_InputRevision = 0;
		std::uint64_t m_SubmittedRevision = 0;
		std::string m_Error;
		std::optional<FnvProfiles> m_Profiles;
		FnvModelConvention m_Convention = FnvModelConvention::Sxdefectalign;
		std::array<bool, 3> m_UseAxis{true, true, true};
		std::array<FnvAxisAlignment, 3> m_Alignments;
		std::array<std::vector<double>, 3> m_ModelPotential;
	};
} // namespace DefectStudio
