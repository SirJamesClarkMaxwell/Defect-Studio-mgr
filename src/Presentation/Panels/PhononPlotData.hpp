#pragma once

#include <array>
#include <optional>
#include <span>
#include <string_view>

#include "IO/DataTableIO.hpp"
#include "IO/PhononDispersionIO.hpp"
#include "Presentation/Figures/PlotData.hpp"
#include "Presentation/Panels/PhononPanelSession.hpp"

namespace DefectStudio
{
	struct PhononPlSelection
	{
		std::string choice;
		std::optional<double> zplEv;
	};
	[[nodiscard]] PhononPlSelection SelectPhononPlCurve(const PhononAnalysisResult &result, std::string_view preferred);
	[[nodiscard]] std::string PhononZplSourceLabel(std::string_view choice);

	struct PhononComparisonRow
	{
		Path directory;
		std::string caseId, caseName, label, zplSource;
		bool compared = false, active = false;
		// ZPL, delta Q, S, DW %, effective energy, strongest energy, S_k, n_eff, imaginary count.
		std::array<std::optional<double>, 9> values;
		std::array<std::string, 9> reasons;
	};
	[[nodiscard]] std::vector<PhononComparisonRow> BuildPhononComparisonRows(PhononPanelSession &session, const Path &project);
	[[nodiscard]] std::vector<DataTableColumn> PhononComparisonColumns(std::span<const PhononComparisonRow> rows);

	// UI supplies the current colormap; adapters resolve automatic colours without depending on ImPlot.
	[[nodiscard]] PlotData BuildBetaPlotData(PhononPanelSession &session, const Path &project,
		const std::vector<ProjectRootEntry> &roots = std::vector<ProjectRootEntry>{}, std::span<const glm::vec4> colors = std::span<const glm::vec4>{});
	[[nodiscard]] PlotData BuildSPlotData(PhononPanelSession &session, const Path &project,
		const std::vector<ProjectRootEntry> &roots = std::vector<ProjectRootEntry>{}, std::span<const glm::vec4> colors = std::span<const glm::vec4>{});
	[[nodiscard]] PlotData BuildPlPlotData(PhononPanelSession &session, const Path &project,
		const std::vector<ProjectRootEntry> &roots = std::vector<ProjectRootEntry>{}, std::span<const glm::vec4> colors = std::span<const glm::vec4>{});
	[[nodiscard]] PlotData BuildCcdPlotData(const CcdAnalysisResult &result, const PhononPanelSettings &settings,
		const Path &source, std::span<const glm::vec4> colors = std::span<const glm::vec4>{});
	[[nodiscard]] PlotData BuildCcdPlPlotData(PhononPanelSession &session, const CcdAnalysisResult &result,
		const Path &project, const std::vector<ProjectRootEntry> &roots = std::vector<ProjectRootEntry>{}, std::span<const glm::vec4> colors = std::span<const glm::vec4>{});
	[[nodiscard]] PlotData BuildDispersionPlotData(const PhononDispersion &result,
		const PhononPanelSettings &settings, const Path &source, std::span<const glm::vec4> colors = std::span<const glm::vec4>{});
	struct DispersionPick
	{
		std::size_t row = 0;
		int band = 0, segment = 0;
		double distance = 0.0, frequencyMeV = 0.0;
		glm::dvec3 q{0.0};
	};
	[[nodiscard]] std::optional<DispersionPick> PickDispersion(const PhononDispersion &result,
		double distance, double frequencyMeV, double distancePerPixel, double meVPerPixel);
	// Returns distance.size() when the segment has no rows.
	[[nodiscard]] std::size_t SnapToSegment(const PhononDispersion &result, int segment, double distance);
	[[nodiscard]] std::string PhononPlotStyleKey(const PlotSeriesData &series);
	[[nodiscard]] std::string PhononPlotLegendId(const PlotSeriesData &series);
	[[nodiscard]] std::vector<DataTableColumn> PhononModesColumns(const PhononAnalysisResult &result,
		std::span<const std::size_t> modes, double carriedFraction, const Path &project);
	void SortPhononModes(const PhononAnalysisResult &result, std::vector<std::size_t> &modes, int column, bool ascending);
	[[nodiscard]] std::string PhononExportLabel(const PhononPanelSession &session, std::string fallback);
	[[nodiscard]] double CcdPlotPointEnergy(const CcdAnalysisResult &result, const CcdPoint &point);
}
