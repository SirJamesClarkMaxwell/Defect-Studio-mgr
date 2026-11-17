#include "Core/dspch.hpp"
#include "Presentation/Panels/PhononPlotData.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <limits>
#include <tuple>

namespace DefectStudio
{
	namespace
	{
		std::string ParameterLabel(const PhononAnalysisResult &result)
		{
			return std::format("{} cutoff={} Å σ={:g} σ_max={} γ={:g} meV ({})",
				result.hrMethod.empty() ? "legacy" : result.hrMethod,
				result.hrCutoffAngstrom ? std::format("{:g}", *result.hrCutoffAngstrom) : "auto",
				result.sigmaMeV, result.sigmaMaxMeV ? std::format("{:g}", *result.sigmaMaxMeV) : "none",
				result.gammaMeV, result.source);
		}

		PhononSeriesMember Member(const PhononAnalysisResult &result)
		{
			PhononSeriesMember member{std::cref(result)};
			member.label = "komórka FC";
			if (result.supercell)
			{
				const auto [a, b, c] = *result.supercell;
				member.n = a == b ? a : static_cast<double>(a) * b * c;
				member.label = std::format("{}x{}x{}", a, b, c);
			}
			double energy = 0.0;
			bool valid = !result.modes.empty();
			for (const auto &mode : result.modes)
			{
				if (!std::isfinite(mode.frequencyMeV)) { valid = false; continue; }
				if (mode.frequencyMeV <= 0.0) continue;
				if (!std::isfinite(mode.huangRhys) || mode.huangRhys < 0.0) { valid = false; continue; }
				energy += mode.huangRhys * mode.frequencyMeV;
				member.irrepS[mode.irrep.empty() ? "???" : mode.irrep] += mode.huangRhys;
			}
			if (valid && std::isfinite(energy))
			{
				member.franckCondonMeV = energy;
				if (std::isfinite(result.sTotal) && result.sTotal > 0.0) member.effectiveMeV = energy / result.sTotal;
			}
			else member.irrepS.clear();
			const auto unknown = member.irrepS.find("???");
			member.uncertainIrreps = unknown != member.irrepS.end() && result.sTotal > 0.0 && unknown->second > 0.1 * result.sTotal;
			return member;
		}
	}

	std::vector<PhononConvergenceSeries> GroupPhononSeries(
		std::span<const std::reference_wrapper<const PhononAnalysisResult>> results)
	{
		using Key = std::tuple<std::string, std::optional<double>, double, std::optional<double>, double, std::string>;
		std::map<Key, PhononConvergenceSeries> groups;
		for (const auto &entry : results)
		{
			const auto &result = entry.get();
			if (!std::isfinite(result.sigmaMeV) || !std::isfinite(result.gammaMeV) ||
				(result.hrCutoffAngstrom && !std::isfinite(*result.hrCutoffAngstrom)) ||
				(result.sigmaMaxMeV && !std::isfinite(*result.sigmaMaxMeV))) continue;
			auto &group = groups[{result.hrMethod, result.hrCutoffAngstrom, result.sigmaMeV,
				result.sigmaMaxMeV, result.gammaMeV, result.source}];
			group.label = ParameterLabel(result);
			// Full precision keeps distinct parameter sets distinct in UI/style identities too.
			group.id = std::format("{}|{}|{}|{}|{}|{}", result.hrMethod,
				result.hrCutoffAngstrom ? std::format("{}", *result.hrCutoffAngstrom) : "auto", result.sigmaMeV,
				result.sigmaMaxMeV ? std::format("{}", *result.sigmaMaxMeV) : "none", result.gammaMeV, result.source);
			group.members.push_back(Member(result));
		}
		std::vector<PhononConvergenceSeries> series;
		for (auto &[key, group] : groups)
		{
			std::stable_sort(group.members.begin(), group.members.end(), [](const auto &a, const auto &b) {
				return std::tuple{a.result.get().supercell.has_value(), a.n, a.label, a.result.get().directory.Utf8()} <
					std::tuple{b.result.get().supercell.has_value(), b.n, b.label, b.result.get().directory.Utf8()};
			});
			series.push_back(std::move(group));
		}
		std::stable_sort(series.begin(), series.end(), [](const auto &a, const auto &b) { return a.members.size() > b.members.size(); });
		return series;
	}

	std::vector<PhononConvergenceSeries> BuildPhononSeries(PhononPanelSession &session, const Path &project, std::string &outError)
	{
		outError.clear();
		std::vector<std::reference_wrapper<const PhononAnalysisResult>> results;
		const auto active = std::find_if(session.Cases().begin(), session.Cases().end(), [&](const auto &item) {
			return item.id == session.ActiveCaseId();
		});
		if (active == session.Cases().end() || active->hidden) return {};
		auto paths = active->datasets;
		if (!session.Settings().resultDirectory.Empty()) paths.push_back(session.Settings().resultDirectory);
		std::vector<Path> visited;
		for (const auto &path : paths)
		{
			const auto owner = session.DatasetCaseId(path);
			if (!owner.empty() && owner != session.ActiveCaseId()) continue;
			if (std::any_of(visited.begin(), visited.end(), [&](const auto &seen) { return PhononPanelSession::SameFilePath(path, seen, project); })) continue;
			visited.push_back(path);
			const auto dataset = std::find_if(session.Datasets().begin(), session.Datasets().end(), [&](const auto &item) {
				return PhononPanelSession::SameFilePath(path, item.directory, project);
			});
			if (dataset != session.Datasets().end() && dataset->kind != AnalysisDatasetKind::Phonons) continue;
			std::string error;
			std::optional<std::reference_wrapper<const PhononAnalysisResult>> result;
			if (session.Result() && PhononPanelSession::SameFilePath(path, session.Settings().resultDirectory, project))
				result = std::cref(*session.Result());
			else result = session.PlotModes(path, error);
			if (!result)
			{
				const auto modeError = error;
				result = session.Spectra(path, error);
				if (result) error = modeError.empty() ? "Brak tabeli modów; brak E_FC, hw_eff i rozkładu irrepów." : modeError;
			}
			if (result) results.push_back(*result);
			if (!error.empty()) outError += PlotSourceComment(path, project) + ": " + error + "\n";
		}
		return GroupPhononSeries(results);
	}

	glm::vec4 PhononSeriesColor(std::size_t index, std::size_t count)
	{
		static const std::array<glm::vec4, 5> stops = {glm::vec4{0.267f, 0.005f, 0.329f, 1},
			glm::vec4{0.230f, 0.322f, 0.546f, 1}, glm::vec4{0.128f, 0.567f, 0.551f, 1},
			glm::vec4{0.369f, 0.789f, 0.383f, 1}, glm::vec4{0.993f, 0.906f, 0.144f, 1}};
		const float position = count > 1 ? 4.0f * static_cast<float>(index) / static_cast<float>(count - 1) : 0.0f;
		const auto first = std::min(static_cast<std::size_t>(position), stops.size() - 2);
		return glm::mix(stops[first], stops[first + 1], position - static_cast<float>(first));
	}

	PlotData BuildPhononSeriesPlot(const PhononConvergenceSeries &series, const PhononPanelSettings &settings, int kind)
	{
		PlotData plot;
		plot.id = "convergence/" + series.id + "/" + std::to_string(kind);
		plot.title = kind == 4 ? "S_irrep" : kind == 5 ? "PL" : kind == 6 ? "S(ℏω)" : "Skalary";
		plot.x = {kind < 5 ? "n" : kind == 6 ? "\\hbar\\omega" : settings.plAxis == "zpl_loss" ? "E_{ZPL} - E" : "E",
			kind < 5 ? "" : kind == 6 || settings.plAxis == "zpl_loss" ? "meV" : "eV"};
		const std::array<std::string, 5> names = {"S", "DW", "\\hbar\\omega_{eff}", "E_{FC}", "S_{irrep}"};
		plot.y = {kind < 5 ? names[kind] : kind == 5 ? "PL" : "S(\\hbar\\omega)",
			kind == 2 || kind == 3 ? "meV" : kind == 6 ? "meV^{-1}" : kind == 5 ? "j.u." : ""};
		if (kind == 5 && settings.plNormalization == "area") plot.y.unit = plot.x.unit + "^{-1}";
		if (series.members.empty()) return plot;
		std::map<std::string, std::size_t> irreps;
		if (kind == 4)
			for (const auto &member : series.members)
				for (const auto &[irrep, value] : member.irrepS) irreps.try_emplace(irrep, irreps.size());
		const auto add = [&](std::string label, const Path &source, glm::vec4 color) -> PlotSeriesData & {
			PlotSeriesData line;
			line.id = plot.id + "/" + label + "/" + source.Utf8(); line.label = std::move(label); line.source = source;
			if (kind < 5 && series.members.size() == 1) line.kind = PlotSeriesKind::Scatter;
			const auto style = settings.seriesStyles.find(line.id);
			if (style != settings.seriesStyles.end()) line.style = style->second;
			if (!line.style.color) line.style.color = color;
			plot.series.push_back(std::move(line));
			return plot.series.back();
		};
		if (kind < 5)
		{
			const auto fill = [&](PlotSeriesData &line, const std::string &irrep) {
				for (const auto &member : series.members)
				{
					const auto &result = member.result.get();
					double value = std::numeric_limits<double>::quiet_NaN();
					if (kind == 0) value = result.sTotal;
					if (kind == 1) value = result.debyeWaller;
					if (kind == 2) value = member.effectiveMeV.value_or(value);
					if (kind == 3) value = member.franckCondonMeV.value_or(value);
					if (kind == 4 && member.franckCondonMeV) value = member.irrepS.contains(irrep) ? member.irrepS.at(irrep) : 0.0;
					line.x.push_back(member.n); line.y.push_back(value);
				}
			};
			if (kind == 4)
				for (const auto &[irrep, index] : irreps) fill(add(irrep, {}, PhononSeriesColor(index, irreps.size())), irrep);
			else fill(add(names[kind], {}, PhononSeriesColor(0, 1)), {});
			plot.x.minimum = series.members.front().n - 0.5; plot.x.maximum = series.members.back().n + 0.5;
		}
		else
			for (std::size_t i = 0; i < series.members.size(); ++i)
			{
				const auto &member = series.members[i]; const auto &result = member.result.get();
				const auto selected = SelectPhononPlCurve(result, settings.plZpl);
				if (kind == 5 && settings.plAxis == "zpl_loss" && !selected.zplEv) continue;
				auto x = kind == 5 ? result.plPhotonEnergyEv : result.spectralEnergyMeV;
				auto y = kind == 5 ? PhononPanelSession::PlCurve(result, selected.choice) : result.spectralDensityPerMeV;
				if (x.empty() || x.size() != y.size()) continue;
				if (kind == 5 && settings.plAxis == "zpl_loss")
				{
					for (auto &value : x) value = 1000.0 * (*selected.zplEv - value);
					std::reverse(x.begin(), x.end()); std::reverse(y.begin(), y.end());
				}
				if (kind == 5) NormalizePhononPl(x, y, settings.plNormalization);
				const auto range = PhononPanelSession::UsefulRange(result, kind == 5, selected.choice);
				const double minimum = kind == 5 && settings.plAxis == "zpl_loss" ? 1000.0 * (*selected.zplEv - range.second) : range.first;
				const double maximum = kind == 5 && settings.plAxis == "zpl_loss" ? 1000.0 * (*selected.zplEv - range.first) : range.second;
				if (plot.series.empty()) { plot.x.minimum = minimum; plot.x.maximum = maximum; }
				else { plot.x.minimum = std::min(plot.x.minimum, minimum); plot.x.maximum = std::max(plot.x.maximum, maximum); }
				auto &line = add(member.label, result.directory, PhononSeriesColor(i, series.members.size()));
				line.x = std::move(x); line.y = std::move(y);
			}
		return plot;
	}
}
