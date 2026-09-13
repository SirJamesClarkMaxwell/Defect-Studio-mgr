#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "Core/JobSystem/JobSystemTypes.hpp"
#include "Core/Diagnostics/StructuredError.hpp"
#include "Core/Utils/Memory.hpp"
#include "Domain/Symmetry/PointGroupAnalysis.hpp"
#include "Domain/Symmetry/PointGroupBasis.hpp"
#include "Presentation/Panels/GroupTheoryFormatting.hpp"
#include "Presentation/Panels/IPanel.hpp"
#include "Renderer/RendererLayer.hpp"

namespace DefectStudio
{
	class DomainLayer;
	class JobSystem;
	class AnalyzePointGroupJob;
	struct StructureRecord;

	class GroupTheoryPanel final : public IPanel
	{
	public:
		explicit GroupTheoryPanel(
			RendererLayer &rendererLayer,
			WeakRef<DomainLayer> domainLayer,
			WeakRef<JobSystem> jobSystem,
			std::string title = "Group Theory",
			bool visibleByDefault = false);
		GroupTheoryPanel(const GroupTheoryPanel &other) = default;

		void Render() override;
		[[nodiscard]] Ref<IPanel> Clone() const override;

	private:
		enum class CentreMode { SelectionCentroid, Cursor, Atom };

		struct BasisKey
		{
			std::string windowId;
			std::string structureId;
			int revision = 0;
			std::uint64_t basisHash = 0;
			bool operator==(const BasisKey &) const = default;
		};

		void drawBasis(const std::string &windowId, const RendererWindowState &windowState, const StructureRecord &record);
		void drawGroupControls();
		void drawResults();
		void drawError();
		void pollJob();
		void buildBasisAndSubmit(const std::string &windowId, const RendererWindowState &windowState, const StructureRecord &record);
		void submitAnalysis();
		[[nodiscard]] std::optional<BasisKey> currentBasisKey() const;

		void drawCharacterTable();
		void drawProjectedVectors();
		void drawMultiplets();
		void copyResults(TableFormat format);

		RendererLayer &m_RendererLayer;
		WeakRef<DomainLayer> m_DomainLayer;
		WeakRef<JobSystem> m_JobSystem;

		CentreMode m_CentreMode = CentreMode::SelectionCentroid;
		std::optional<SelectionBasis> m_Basis;
		std::optional<BasisKey> m_BasisKey;
		int m_GroupIndex = 0;
		double m_Tolerance = 0.1;
		std::array<char, 256> m_ActiveIrreps{};
		int m_Electrons = 0;

		Ref<AnalyzePointGroupJob> m_PendingJob;
		JobId m_PendingJobId = 0;
		std::optional<BasisKey> m_SubmittedKey;
		std::optional<PointGroupAnalysisResult> m_Result;
		std::optional<StructuredError> m_Error;
		std::vector<std::array<char, 128>> m_PhysicalBuffers;
		std::vector<std::size_t> m_VectorOrder;
	};
} // namespace DefectStudio
