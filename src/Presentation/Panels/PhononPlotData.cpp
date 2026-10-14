#include "Core/dspch.hpp"
#include "Presentation/Panels/PhononPlotData.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <iterator>
#include <limits>
#include <map>

namespace DefectStudio
{
	namespace
	{
		PlotSeriesStyle Style(const PhononPanelSettings &settings, const std::string &key)
		{
			const auto found = settings.seriesStyles.find(key);
			return found == settings.seriesStyles.end() ? PlotSeriesStyle{} : found->second;
		}

		PlotData PhononPlot(PhononPanelSession &session, const Path &project,
			const std::vector<ProjectRootEntry> &roots, int tab, std::span<const glm::vec4> colors)
		{
			const auto &settings = session.Settings();
			PlotData plot;
			plot.id = tab == 0 ? "beta" : tab == 1 ? "S" : "PL";
			plot.title = tab == 0 ? "β(ℏω)" : tab == 1 ? "S_k i S(ℏω)" : "PL";
			plot.x = {tab == 2 ? "E, E_{ZPL}" : "\\hbar\\omega", tab == 2 ? "eV" : "meV"};
			plot.y = {tab == 0 ? "\\beta = N/N_{eff}" : tab == 1 ? "S_k" : "PL", tab == 2 ? "j.u." : ""};
			plot.hasY2 = tab == 1;
			plot.y2 = {"S(\\hbar\\omega)", "meV^{-1}"};
			std::optional<std::reference_wrapper<const PhononAnalysisResult>> primary;
			Path primarySource = settings.resultDirectory;
			if (const auto active = session.Result()) primary = std::cref(*active);
			else
				for (const auto &dataset : session.Datasets())
					if (dataset.kind == AnalysisDatasetKind::Phonons && session.IsCompared(dataset.directory))
					{
						std::string error;
						primary = tab == 0 ? session.PlotModes(dataset.directory, error) : session.Spectra(dataset.directory, error);
						if (primary) { primarySource = dataset.directory; break; }
					}
			if (!primary) return plot;
			const auto [low, high] = PhononPanelSession::UsefulRange(primary->get(), tab == 2, settings.plZpl);
			plot.x.minimum = low; plot.x.maximum = high;
			if (tab > 0)
				for (const auto &dataset : session.Datasets())
					if (dataset.kind == AnalysisDatasetKind::Phonons && session.IsCompared(dataset.directory) &&
						(!session.Result() || !PhononPanelSession::SameFilePath(dataset.directory, settings.resultDirectory, project)))
					{
						std::string error;
						if (const auto result = session.Spectra(dataset.directory, error))
						{
							const auto [a, b] = PhononPanelSession::UsefulRange(result->get(), tab == 2, settings.plZpl);
							plot.x.minimum = std::min(plot.x.minimum, a); plot.x.maximum = std::max(plot.x.maximum, b);
						}
					}
			if (tab < 2)
			{
				auto modes = session.Result() ? session.VisibleModes() : std::vector<std::size_t>{};
				if (!session.Result())
					for (std::size_t i = 0; i < primary->get().modes.size(); ++i)
					{
						const auto &mode = primary->get().modes[i];
						if (!mode.isTranslation && mode.huangRhys >= settings.minHuangRhys &&
							(settings.irrepFilter.empty() || settings.irrepFilter == mode.irrep)) modes.push_back(i);
					}
				for (const auto &name : {"other", "A1"})
				{
					PlotSeriesData series;
					series.id = plot.id + "/" + name;
					series.label = std::string_view(name) == "A1" ? "A1" : "inne irreps";
					series.kind = PlotSeriesKind::Stems;
					series.source = primarySource;
					series.style = Style(settings, series.id);
					for (const auto index : modes)
					{
						const auto &mode = primary->get().modes[index];
						if ((mode.irrep == "A1") == (std::string_view(name) == "A1"))
						{
							series.x.push_back(mode.frequencyMeV);
							series.y.push_back(tab == 0 ? mode.beta : mode.huangRhys);
						}
					}
					plot.series.push_back(std::move(series));
				}
				if (const auto selected = session.SelectedMode())
				{
					PlotSeriesData series;
					series.id = plot.id + "/selected"; series.label = "Wybrany mod";
					series.kind = PlotSeriesKind::VerticalLine;
					series.x = {primary->get().modes[*selected].frequencyMeV};
					series.source = primarySource; series.style = Style(settings, series.id);
					plot.series.push_back(std::move(series));
				}
			}
			const auto addSpectrum = [&](const PhononAnalysisResult &result, const Path &source, const std::string &label) {
				const auto &x = tab == 2 ? result.plPhotonEnergyEv : result.spectralEnergyMeV;
				const auto &y = tab == 2 ? PhononPanelSession::PlCurve(result, PhononPanelSession::PlCurveChoice(result, settings.plZpl)) : result.spectralDensityPerMeV;
				if (x.empty() || y.size() != x.size()) return;
				const auto [first, last] = PhononPanelSession::GridIndexRange(x, plot.x.minimum, plot.x.maximum);
				PlotSeriesData series;
				series.id = plot.id + "/" + PhononPanelSession::DirectoryLabel(source, project, roots);
				series.label = label;
				series.x.assign(x.begin() + first, x.begin() + last); series.y.assign(y.begin() + first, y.begin() + last);
				series.yAxis = tab == 1 ? 1 : 0;
				series.source = source; series.style = Style(settings, series.id);
				plot.series.push_back(std::move(series));
			};
			if (tab > 0)
			{
				if (const auto active = session.Result())
					addSpectrum(*active, settings.resultDirectory, active->label.empty() ? settings.resultDirectory.filename().Utf8() : active->label);
				for (const auto &dataset : session.Datasets())
					if (dataset.kind == AnalysisDatasetKind::Phonons && session.IsCompared(dataset.directory) &&
						(!session.Result() || !PhononPanelSession::SameFilePath(dataset.directory, settings.resultDirectory, project)))
					{
						std::string error;
						if (const auto result = session.Spectra(dataset.directory, error)) addSpectrum(result->get(), dataset.directory, dataset.label);
					}
			}
			ResolvePlotColors(plot, colors);
			return plot;
		}

		void AddLine(PlotData &plot, const PhononPanelSettings &settings, const std::string &id,
			const std::string &label, const Path &source, const std::vector<double> &x, const std::vector<double> &y, bool keepEmpty = false)
		{
			if ((!keepEmpty && x.empty()) || y.size() != x.size()) return;
			const auto [first, last] = PhononPanelSession::GridIndexRange(x, plot.x.minimum, plot.x.maximum);
			PlotSeriesData series;
			series.id = id; series.label = label; series.source = source; series.style = Style(settings, id);
			series.x.assign(x.begin() + first, x.begin() + last); series.y.assign(y.begin() + first, y.begin() + last);
			plot.series.push_back(std::move(series));
		}
	}

	void ResolvePlotColors(PlotData &plot, std::span<const glm::vec4> colors)
	{
		std::map<std::string, std::size_t> legends;
		for (auto &series : plot.series)
		{
			const auto index = legends.try_emplace(series.label + "/" + series.source.Utf8(), legends.size()).first->second;
			if (!series.style.color && !colors.empty()) series.style.color = colors[index % colors.size()];
		}
	}

	PlotData BuildBetaPlotData(PhononPanelSession &session, const Path &project,
		const std::vector<ProjectRootEntry> &roots, std::span<const glm::vec4> colors)
	{
		return PhononPlot(session, project, roots, 0, colors);
	}

	PlotData BuildSPlotData(PhononPanelSession &session, const Path &project,
		const std::vector<ProjectRootEntry> &roots, std::span<const glm::vec4> colors)
	{
		return PhononPlot(session, project, roots, 1, colors);
	}

	PlotData BuildPlPlotData(PhononPanelSession &session, const Path &project,
		const std::vector<ProjectRootEntry> &roots, std::span<const glm::vec4> colors)
	{
		return PhononPlot(session, project, roots, 2, colors);
	}

	PlotData BuildDispersionPlotData(const PhononDispersion &result,
		const PhononPanelSettings &settings, const Path &source, std::span<const glm::vec4> colors)
	{
		PlotData plot;
		plot.id = "dispersion"; plot.title = "Dyspersja";
		plot.y = {"\\hbar\\omega", "meV", std::min(0.0, result.minMeV) - 5.0, std::max(0.0, result.maxMeV) + 5.0};
		if (result.distance.empty()) return plot;
		plot.x.minimum = result.distance.front(); plot.x.maximum = std::max(result.distance.back(), result.distance.front() + 1e-6);
		for (int band = 0; band < result.bandCount; ++band)
		{
			PlotSeriesData series;
			series.id = "dispersion/bands/" + std::to_string(band); series.label = "Pasma";
			series.kind = PlotSeriesKind::Segments; series.source = source;
			series.style = Style(settings, "dispersion/bands");
			for (std::size_t i = 0; i < result.distance.size(); ++i)
			{
				if (i > 0 && result.segment[i] != result.segment[i - 1])
				{
					series.x.push_back(std::numeric_limits<double>::quiet_NaN());
					series.y.push_back(std::numeric_limits<double>::quiet_NaN());
				}
				series.x.push_back(result.distance[i]); series.y.push_back(result.bandsMeV[band][i]);
			}
			plot.series.push_back(std::move(series));
		}
		ResolvePlotColors(plot, colors);
		return plot;
	}

	std::string PhononPlotStyleKey(const PlotSeriesData &series)
	{
		for (const std::string_view group : {"CCD/points", "CCD/levels", "CCD/chi_gs", "CCD/chi_es", "dispersion/bands"})
			if (series.id.starts_with(std::string(group) + "/")) return std::string(group);
		return series.id;
	}

	std::string PhononPlotLegendId(const PlotSeriesData &series)
	{
		if (series.kind == PlotSeriesKind::Line && (series.id.starts_with("S/") || series.id.starts_with("PL/")))
			return series.source.Utf8();
		if (series.id.starts_with("PL1D/phonons/")) return "phononPL";
		if (series.id.starts_with("PL1D/") && series.id != "PL1D/numerov" &&
			series.id != "PL1D/harmonic" && series.id != "PL1D/phonon1d") return series.source.Utf8();
		return {};
	}

	std::string PlotSourceComment(const Path &source, const Path &project)
	{
		if (source.Empty()) return {};
		if (project.Empty()) return source.Utf8();
		const auto relative = source.Native().lexically_normal().lexically_relative(project.Native().lexically_normal());
		if (relative.empty()) return source.Utf8();
		const auto text = relative.generic_u8string();
		return std::string(text.begin(), text.end());
	}

	std::string PhononExportLabel(const PhononPanelSession &session, std::string fallback)
	{
		for (const auto &entry : session.Cases())
			if (entry.id == session.ActiveCaseId() && !entry.name.empty()) return entry.name;
		return fallback.empty() ? "dataset" : fallback;
	}

	std::vector<DataTableColumn> PlotDataColumns(const PlotData &plot, const Path &project, bool onlyVisible)
	{
		std::vector<DataTableColumn> columns;
		for (const auto &series : plot.series)
		{
			if (onlyVisible && !series.style.visible) continue;
			const auto label = series.label.substr(0, series.label.find("###"));
			const auto comment = PlotSourceComment(series.source, project);
			columns.push_back({label + " x", plot.x.unit, comment, series.x, {}});
			if (series.kind != PlotSeriesKind::VerticalLine)
				columns.push_back({label + " y", series.yAxis == 1 ? plot.y2.unit : plot.y.unit, comment, series.y, {}});
		}
		return columns;
	}

	std::vector<DataTableColumn> PhononModesColumns(const PhononAnalysisResult &result,
		std::span<const std::size_t> modes, double carriedFraction, const Path &project)
	{
		const auto comment = PlotSourceComment(result.directory, project);
		std::vector<DataTableColumn> columns = {{"#", "", comment, {}, {}}, {"ℏω", "meV", comment, {}, {}},
			{"irrep", "", comment, {}, {}}, {"β = N/N_eff", "", comment, {}, {}}, {"N_eff", "", comment, {}, {}},
			{"S_k", "", comment, {}, {}}, {std::format("N({:.0f}%)", carriedFraction * 100.0), "", comment, {}, {}}};
		for (const auto index : modes)
		{
			const auto &mode = result.modes[index];
			columns[0].values.push_back(mode.index); columns[1].values.push_back(mode.frequencyMeV);
			columns[2].text.push_back(mode.irrep); columns[3].values.push_back(mode.beta);
			columns[4].values.push_back(mode.effectiveAtomCount); columns[5].values.push_back(mode.huangRhys);
			columns[6].values.push_back(static_cast<double>(AtomsCarryingFraction(result, index, carriedFraction)));
		}
		return columns;
	}

	void SortPhononModes(const PhononAnalysisResult &result, std::vector<std::size_t> &modes, int column, bool ascending)
	{
		std::stable_sort(modes.begin(), modes.end(), [&](auto a, auto b) {
			const auto &left = result.modes[a], &right = result.modes[b];
			if (column == 2)
				return ascending ? left.irrep < right.irrep : right.irrep < left.irrep;
			const auto value = [&](const PhononMode &mode) {
				switch (column)
				{
				case 0: return static_cast<double>(mode.index);
				case 3: return mode.beta;
				case 4: return mode.effectiveAtomCount;
				case 5: return mode.huangRhys;
				default: return mode.frequencyMeV;
				}
			};
			return ascending ? value(left) < value(right) : value(right) < value(left);
		});
	}

	double CcdPlotPointEnergy(const CcdAnalysisResult &result, const CcdPoint &point)
	{
		if (point.energyEv) return *point.energyEv;
		const auto &grid = result.curves.q;
		const auto &values = point.branch == "es" ? result.curves.vEsEv : result.curves.vGsEv;
		if (point.branch == "triplet" || grid.empty() || values.size() != grid.size() || point.q < grid.front() || point.q > grid.back()) return 0.0;
		const auto it = std::lower_bound(grid.begin(), grid.end(), point.q);
		const auto i = static_cast<std::size_t>(it - grid.begin());
		if (i == 0 || *it == point.q) return values[i];
		return std::lerp(values[i - 1], values[i], (point.q - grid[i - 1]) / (grid[i] - grid[i - 1]));
	}

	PlotData BuildCcdPlotData(const CcdAnalysisResult &result, const PhononPanelSettings &settings,
		const Path &source, std::span<const glm::vec4> colors)
	{
		PlotData plot;
		plot.id = "CCD"; plot.title = "CCD";
		plot.x = {"Q", "amu^{1/2} Å", std::min(0.0, result.deltaQ), std::max(0.0, result.deltaQ)};
		plot.y = {"E", "eV"};
		for (bool excited : {false, true})
			for (int n = 0; n < settings.ccdLevels; ++n)
				if (const auto range = PhononPanelSession::CcdLevelRange(result, excited, n))
				{ plot.x.minimum = std::min(plot.x.minimum, range->first); plot.x.maximum = std::max(plot.x.maximum, range->second); }
		for (const auto &point : result.points)
		{ plot.x.minimum = std::min(plot.x.minimum, point.q); plot.x.maximum = std::max(plot.x.maximum, point.q); }
		const double margin = 0.25 * std::max(plot.x.maximum - plot.x.minimum, 0.2);
		plot.x.minimum -= margin; plot.x.maximum += margin;
		AddLine(plot, settings, "CCD/gs", "V_gs", source, result.curves.q, result.curves.vGsEv, true);
		AddLine(plot, settings, "CCD/es", "V_es", source, result.curves.q, result.curves.vEsEv, true);
		ResolvePlotColors(plot, colors);
		const auto branchColor = [&](bool excited) -> std::optional<glm::vec4> {
			const auto style = Style(settings, excited ? "CCD/es" : "CCD/gs");
			if (style.color || colors.empty()) return style.color;
			return colors[(excited ? 1 : 0) % colors.size()];
		};
		for (const auto &point : result.points)
		{
			const auto id = "CCD/points/" + point.branch + (point.status == "ok" ? "/filled" : "/hollow");
			auto found = std::find_if(plot.series.begin(), plot.series.end(), [&](const auto &series) { return series.id == id; });
			if (found == plot.series.end())
			{
				PlotSeriesData series;
				series.id = id; series.label = "Punkty"; series.kind = PlotSeriesKind::Scatter;
				series.source = source; series.style = Style(settings, "CCD/points");
				if (!series.style.color)
					series.style.color = point.branch == "gs" ? branchColor(false) : point.branch == "es" ? branchColor(true) : std::optional(glm::vec4(170.0f / 255, 110.0f / 255, 230.0f / 255, 1));
				series.marker.shape = point.branch == "triplet" ? PlotMarker::Shape::Diamond : PlotMarker::Shape::Circle;
				series.marker.filled = point.status == "ok";
				plot.series.push_back(std::move(series));
				found = std::prev(plot.series.end());
			}
			found->x.push_back(point.q); found->y.push_back(CcdPlotPointEnergy(result, point));
		}
		for (bool excited : {false, true})
		{
			const auto &levels = excited ? result.levels.esMeV : result.levels.gsMeV;
			const auto &waves = excited ? result.wavefunctions.es : result.wavefunctions.gs;
			for (std::size_t n = 0; n < std::min(levels.size(), static_cast<std::size_t>(settings.ccdLevels)); ++n)
			{
				const double energy = (excited ? result.zplEv : 0) + levels[n] / 1000;
				if (const auto range = PhononPanelSession::CcdLevelRange(result, excited, n))
				{
					PlotSeriesData series;
					series.id = std::string("CCD/levels/") + (excited ? "es/" : "gs/") + std::to_string(n);
					series.label = "Poziomy"; series.source = source; series.style = Style(settings, "CCD/levels");
					if (!series.style.color) series.style.color = branchColor(excited);
					series.x = {range->first, range->second}; series.y = {energy, energy};
					plot.series.push_back(std::move(series));
				}
				if (!settings.ccdWavefunctions || n >= waves.size() || waves[n].size() != result.wavefunctions.q.size()) continue;
				const auto [a, b] = PhononPanelSession::GridIndexRange(result.wavefunctions.q, plot.x.minimum, plot.x.maximum);
				PlotSeriesData series;
				const std::string key = excited ? "CCD/chi_es" : "CCD/chi_gs";
				series.id = key + "/" + std::to_string(n); series.label = excited ? "χ es" : "χ gs";
				series.source = source; series.style = Style(settings, key);
				if (!series.style.color) series.style.color = branchColor(excited);
				series.x.assign(result.wavefunctions.q.begin() + a, result.wavefunctions.q.begin() + b);
				for (std::size_t i = a; i < b; ++i)
					series.y.push_back(energy + settings.ccdWavefunctionScale * (settings.ccdProbability ? waves[n][i] * waves[n][i] : waves[n][i]));
				plot.series.push_back(std::move(series));
			}
		}
		ResolvePlotColors(plot, colors);
		return plot;
	}

	PlotData BuildCcdPlPlotData(PhononPanelSession &session, const CcdAnalysisResult &result,
		const Path &project, const std::vector<ProjectRootEntry> &roots, std::span<const glm::vec4> colors)
	{
		const auto &settings = session.Settings();
		PlotData plot;
		plot.id = "PL1D"; plot.title = "PL 1D";
		const auto [minimum, maximum] = PhononPanelSession::UsefulRange(result);
		plot.x = {"E", "eV", minimum, maximum}; plot.y = {"PL 1D", "j.u."};
		for (const auto &dataset : session.Datasets())
			if (dataset.kind == AnalysisDatasetKind::Ccd && session.IsCompared(dataset.directory) &&
				!PhononPanelSession::SameFilePath(dataset.directory, settings.selectedCcdDirectory, project))
			{
				std::string error;
				if (const auto ccd = session.Ccd(dataset.directory, error))
				{
					const auto [low, high] = PhononPanelSession::UsefulRange(ccd->get());
					plot.x.minimum = std::min(plot.x.minimum, low); plot.x.maximum = std::max(plot.x.maximum, high);
				}
			}
		if (settings.ccdComparePhononPl && session.Result())
		{
			const auto [low, high] = PhononPanelSession::UsefulRange(*session.Result(), true, settings.plZpl);
			plot.x.minimum = std::min(plot.x.minimum, low); plot.x.maximum = std::max(plot.x.maximum, high);
		}
		AddLine(plot, settings, "PL1D/numerov", "Numerov", settings.selectedCcdDirectory, result.pl1d.energyEv, result.pl1d.numerov);
		AddLine(plot, settings, "PL1D/harmonic", "harmoniczne", settings.selectedCcdDirectory, result.pl1d.energyEv, result.pl1d.harmonic);
		AddLine(plot, settings, "PL1D/phonon1d", "fonon 1D", settings.selectedCcdDirectory, result.pl1d.energyEv, result.pl1d.phonon1d);
		if (settings.ccdComparePhononPl && session.Result())
		{
			const auto &phonons = *session.Result();
			const auto path = PhononPanelSession::DirectoryLabel(settings.resultDirectory, project, roots);
			AddLine(plot, settings, "PL1D/phonons/" + path, (phonons.label.empty() ? path : phonons.label) + " (fonony)",
				settings.resultDirectory, phonons.plPhotonEnergyEv, PhononPanelSession::PlCurve(phonons, PhononPanelSession::PlCurveChoice(phonons, settings.plZpl)));
		}
		for (const auto &dataset : session.Datasets())
			if (dataset.kind == AnalysisDatasetKind::Ccd && session.IsCompared(dataset.directory) &&
				!PhononPanelSession::SameFilePath(dataset.directory, settings.selectedCcdDirectory, project))
			{
				std::string error;
				if (const auto ccd = session.Ccd(dataset.directory, error))
					AddLine(plot, settings, "PL1D/" + PhononPanelSession::DirectoryLabel(dataset.directory, project, roots),
						dataset.label, dataset.directory, ccd->get().pl1d.energyEv, ccd->get().pl1d.numerov);
			}
		ResolvePlotColors(plot, colors);
		return plot;
	}
}
