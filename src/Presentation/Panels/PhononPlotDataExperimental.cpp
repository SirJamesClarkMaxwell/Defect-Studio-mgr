#include "Core/dspch.hpp"
#include "Presentation/Panels/PhononPlotData.hpp"

#include <algorithm>
#include <cmath>
#include <format>

namespace DefectStudio
{
	void NormalizePhononPl(std::span<const double> x, std::vector<double> &y, std::string_view normalization)
	{
		if (normalization == "none" || y.empty() || x.size() != y.size()) return;
		double scale = 0.0;
		if (normalization == "max") scale = *std::max_element(y.begin(), y.end());
		else
			for (std::size_t i = 1; i < x.size(); ++i)
				scale += 0.5 * (y[i - 1] + y[i]) * (x[i] - x[i - 1]);
		if (std::isfinite(scale) && scale > 0.0)
			for (auto &value : y) value /= scale;
	}

	namespace
	{
		std::string SeriesId(const PhononCase &item, std::size_t index)
		{
			return "pl/exp/" + item.id + "/" + std::to_string(index);
		}

		PhononPlSelection CasePl(PhononPanelSession &session, const PhononCase &item)
		{
			auto result = session.CachedCaseResult(item.id);
			if (!result && !item.settings.resultDirectory.Empty())
			{
				std::string error;
				result = session.Spectra(item.settings.resultDirectory, error);
			}
			return result ? SelectPhononPlCurve(result->get(), session.Settings().plZpl) : PhononPlSelection{};
		}
	}

	void AppendExperimentalPl(PlotData &plot, PhononPanelSession &session, const Path &project, std::span<const glm::vec4> colors)
	{
		const auto &settings = session.Settings();
		bool hasRange = plot.x.maximum > plot.x.minimum;
		std::size_t colorIndex = 0;
		for (bool active : {true, false})
			for (const auto &item : session.Cases())
			{
				if (item.hidden || item.experimentalSpectra.empty() || (item.id == session.ActiveCaseId()) != active || (!active &&
					!session.IsCompared(item.settings.resultDirectory) && !session.IsCompared(item.settings.selectedCcdDirectory) &&
					std::none_of(item.datasets.begin(), item.datasets.end(), [&](const auto &path) { return session.IsCompared(path); }))) continue;
				const auto fallback = CasePl(session, item);
				std::optional<glm::vec4> color;
				for (const auto &series : plot.series)
					if (series.id.starts_with("PL/") && (session.DatasetCaseId(series.source) == item.id ||
						PhononPanelSession::SameFilePath(series.source, item.settings.resultDirectory, project)))
					{ color = series.style.color; break; }
				if (!color && !colors.empty()) color = colors[colorIndex % colors.size()];
				++colorIndex;
				for (std::size_t index = 0; index < item.experimentalSpectra.size(); ++index)
				{
					const auto &source = item.experimentalSpectra[index];
					if (!source.visible) continue;
					PlotSeriesData series;
					series.id = SeriesId(item, index);
					series.label = (source.label.empty() ? source.file.filename().Utf8() : source.label) + " (eksp.)";
					series.source = source.file;
					series.style.lineStyle = 1;
					series.style.lineWeight = 1.0f;
					if (const auto found = settings.seriesStyles.find(series.id); found != settings.seriesStyles.end()) series.style = found->second;
					if (!series.style.color) series.style.color = color;
					const auto &data = session.Experimental(source);
					const auto zpl = source.zplEv ? source.zplEv : fallback.zplEv;
					if (data.spectrum && (settings.plAxis != "zpl_loss" || (zpl && std::isfinite(*zpl))))
					{
						series.x = data.spectrum->energyEv; series.y = data.spectrum->intensity;
						if (settings.plAxis == "zpl_loss")
						{
							for (auto &value : series.x) value = 1000.0 * (*zpl - value);
							std::reverse(series.x.begin(), series.x.end()); std::reverse(series.y.begin(), series.y.end());
						}
						NormalizePhononPl(series.x, series.y, settings.plNormalization);
						plot.x.minimum = hasRange ? std::min(plot.x.minimum, series.x.front()) : series.x.front();
						plot.x.maximum = hasRange ? std::max(plot.x.maximum, series.x.back()) : series.x.back();
						hasRange = true;
					}
					// Empty lines use the existing legend dummy; the tooltip explains why no curve was drawn.
					plot.series.push_back(std::move(series));
				}
			}
	}

	std::string PhononExperimentalSeriesNote(PhononPanelSession &session, const PlotSeriesData &series)
	{
		for (const auto &item : session.Cases())
			for (std::size_t index = 0; index < item.experimentalSpectra.size(); ++index)
				if (series.id == SeriesId(item, index))
				{
					const auto &source = item.experimentalSpectra[index];
					const auto &data = session.Experimental(source);
					std::string note = source.file.Utf8() + "\n";
					if (data.loading) return note + "Wczytywanie…";
					if (!data.error.empty()) return note + data.error;
					if (source.zplEv) return note + std::format("ZPL widma: {:.6g} eV.", *source.zplEv);
					const auto fallback = CasePl(session, item);
					if (fallback.zplEv) return note + std::format("ZPL krzywej obliczonej ({}): {:.6g} eV.",
						PhononZplSourceLabel(fallback.choice), *fallback.zplEv);
					return note + (session.Settings().plAxis == "zpl_loss" ?
						"Brak ZPL widma i krzywej obliczonej; pominięto na osi E_ZPL − E." : "Brak ZPL do wyrównania osi E_ZPL − E; oś E używa energii zmierzonej.");
				}
		return {};
	}
}
