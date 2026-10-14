#pragma once

#include <span>

#include "IO/DataTableIO.hpp"
#include "IO/PhononDispersionIO.hpp"
#include "Presentation/Figures/PlotData.hpp"
#include "Presentation/Panels/PhononPanelSession.hpp"

namespace DefectStudio
{
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
	[[nodiscard]] std::string PhononPlotStyleKey(const PlotSeriesData &series);
	[[nodiscard]] std::string PhononPlotLegendId(const PlotSeriesData &series);
	[[nodiscard]] std::vector<DataTableColumn> PhononModesColumns(const PhononAnalysisResult &result,
		std::span<const std::size_t> modes, double carriedFraction, const Path &project);
	void SortPhononModes(const PhononAnalysisResult &result, std::vector<std::size_t> &modes, int column, bool ascending);
	[[nodiscard]] std::string PhononExportLabel(const PhononPanelSession &session, std::string fallback);
	[[nodiscard]] double CcdPlotPointEnergy(const CcdAnalysisResult &result, const CcdPoint &point);
}
