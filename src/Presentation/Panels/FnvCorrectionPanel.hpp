#pragma once

#include <array>
#include <functional>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "Core/Utils/Memory.hpp"
#include "IO/FnvCorrectionsIO.hpp"
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
		// The project's saved corrections (EditorLayer, on project open). Does not call the handler.
		void SetPersistedState(FnvCorrectionsFile file);
		// Called with the whole file after every change worth keeping; EditorLayer writes it to disk.
		void SetSaveHandler(std::function<void(const FnvCorrectionsFile &)> handler) { m_SaveHandler = std::move(handler); }

	private:
		void drawData();
		void drawSavedEntries(float labelWidth);
		void loadSavedEntry();
		bool selectSavedLocpot(const Path &path);
		[[nodiscard]] PersistedFnvCorrection currentEntry() const;
		void persistChanges();
		void publishSavedState();
		void drawResult();
		void drawPlots();
		void plotAxis(int axis, bool editable);
		void proposeCentre();
		void syncChargeMarker(bool create);
		void invalidateProfiles();
		void submitProfiles();
		void pollJob();
		void proposeWindows();
		void rebuildAlignments(const std::optional<std::array<FnvPlateauWindow, 3>> &windows);
		void copyResult() const;

		RendererLayer &m_RendererLayer;
		WeakRef<JobSystem> m_JobSystem;
		std::function<void(const FnvCorrectionsFile &)> m_SaveHandler;
		// Shared with Clone()s, so two FNV windows never save diverging copies of the project's file.
		Ref<FnvCorrectionsFile> m_Saved = CreateRef<FnvCorrectionsFile>();
		bool m_Dirty = false;
		std::vector<std::string> m_SavedLabels;
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
		std::array<std::string, 3> m_AxisLabels{"a1", "a2", "a3"};
		std::string m_CentreSource; // where the centre came from, shown under the field
		bool m_ShowDeltaV = false;
		bool m_ShowModel = false;
		int m_EditedAxis = 0; // the axis whose plateau window the plot's handles move
		// task/95: the charge marker's fractional position after the last sync, and whose it was.
		std::optional<glm::dvec3> m_MarkerCentre;
		std::string m_MarkerKey;
	};
} // namespace DefectStudio
