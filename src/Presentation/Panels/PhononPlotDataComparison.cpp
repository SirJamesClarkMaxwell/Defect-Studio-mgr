#include "Core/dspch.hpp"
#include "Presentation/Panels/PhononPlotData.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <limits>
#include "Domain/Vibrations/DisplacementAnalysis.hpp"

namespace DefectStudio
{
	namespace
	{
		std::optional<std::reference_wrapper<const CcdDisplacement>> CaseDisplacement(
			PhononPanelSession &session, const PhononCase &item, std::string &reason)
		{
			if (item.settings.selectedCcdDirectory.Empty())
			{
				reason = "Brak zestawu CCD z ΔR w przypadku.";
				return std::nullopt;
			}
			std::string error;
			const auto result = session.Ccd(item.settings.selectedCcdDirectory, error);
			if (result && result->get().displacement) return std::cref(*result->get().displacement);
			reason = "Zestaw CCD bez ΔR - przelicz CCD.";
			if (!error.empty()) reason += " " + error;
			return std::nullopt;
		}

		struct ComparedDisplacement
		{
			std::string id, name;
			Path source;
			std::reference_wrapper<const CcdDisplacement> displacement;
		};

		std::vector<ComparedDisplacement> ComparedDisplacements(PhononPanelSession &session)
		{
			std::vector<ComparedDisplacement> result;
			for (bool active : {true, false})
				for (const auto &item : session.Cases())
					if ((item.id == session.ActiveCaseId()) == active && (active ||
						session.IsCompared(item.settings.resultDirectory) || session.IsCompared(item.settings.selectedCcdDirectory) ||
						std::any_of(item.datasets.begin(), item.datasets.end(), [&](const auto &path) { return session.IsCompared(path); })))
					{
						std::string reason;
						if (const auto displacement = CaseDisplacement(session, item, reason))
							result.push_back({item.id, item.name, item.settings.selectedCcdDirectory, *displacement});
					}
			return result;
		}

		void FillDisplacement(PhononComparisonRow &row, PhononPanelSession &session)
		{
			std::string reason = "Brak zestawu CCD z ΔR w przypadku.";
			const auto &cases = session.Cases();
			const auto found = std::find_if(cases.begin(), cases.end(), [&](const auto &item) { return item.id == row.caseId; });
			const auto displacement = found == cases.end() ? std::nullopt : CaseDisplacement(session, *found, reason);
			if (displacement)
			{
				const auto &data = displacement->get();
				row.values[9] = data.deltaQ;
				double maximum = 0.0;
				for (const auto &vector : data.displacement) maximum = std::max(maximum, glm::length(vector));
				row.values[10] = maximum;
				row.values[11] = static_cast<double>(AtomsCarrying(data, session.Settings().carriedFraction));
			}
			else
				for (std::size_t i = 9; i < 12; ++i) row.reasons[i] = reason;
		}

		void FillComparison(PhononComparisonRow &row, const PhononAnalysisResult &result, std::string_view preferred,
			bool hasModes, const std::string &modesError)
		{
			const auto set = [&](std::size_t column, double value) {
				if (std::isfinite(value)) row.values[column] = value;
				else row.reasons[column] = "Wartość nie jest skończona.";
			};
			auto selected = SelectPhononPlCurve(result, preferred);
			if (PhononPanelSession::PlCurve(result, selected.choice).empty())
			{
				// Without PL data, the summary can still supply the requested ZPL or a recorded fallback.
				for (const auto key : {std::string(preferred), std::string("computed"), std::string("spin_purified"),
					std::string("experimental"), std::string("formula")})
				{
					const auto value = key == "formula" ? result.zplEv.formula : key == "experimental" ? result.zplEv.experimental :
						key == "spin_purified" ? result.zplEv.spinPurified : result.zplEv.computed;
					if (value && std::isfinite(*value)) { selected = {key, value}; break; }
				}
			}
			row.values[0] = selected.zplEv;
			row.zplSource = selected.zplEv ? PhononZplSourceLabel(selected.choice) : "Brak dostępnego ZPL w podsumowaniu.";
			if (!selected.zplEv) row.reasons[0] = row.zplSource;
			set(1, result.deltaQAmuHalfAngstrom); set(2, result.sTotal); set(3, 100.0 * result.debyeWaller);
			if (!hasModes)
			{
				for (std::size_t i = 4; i < row.values.size(); ++i)
					row.reasons[i] = modesError.empty() ? "Brak tabeli modów." : modesError;
				return;
			}
			set(8, static_cast<double>(result.imaginaryModes.size()));
			std::optional<std::size_t> strongest;
			double q2 = 0.0, weighted = 0.0, largestQ2 = 0.0;
			bool valid = true;
			for (std::size_t i = 0; i < result.modes.size(); ++i)
			{
				const auto &mode = result.modes[i];
				if (mode.isTranslation) continue;
				if (std::isfinite(mode.huangRhys) && (!strongest || mode.huangRhys > result.modes[*strongest].huangRhys)) strongest = i;
				valid = valid && std::isfinite(mode.frequencyMeV) && mode.frequencyMeV > 0.0 && std::isfinite(mode.qAmuHalfAngstrom);
				q2 += mode.qAmuHalfAngstrom * mode.qAmuHalfAngstrom;
				largestQ2 = std::max(largestQ2, mode.qAmuHalfAngstrom * mode.qAmuHalfAngstrom);
				const double product = mode.frequencyMeV * mode.qAmuHalfAngstrom;
				weighted += product * product;
			}
			const double deltaQ2 = result.deltaQAmuHalfAngstrom * result.deltaQAmuHalfAngstrom;
			if (std::isfinite(q2) && std::isfinite(deltaQ2) && result.deltaQAmuHalfAngstrom > 0.0 && deltaQ2 > 0.0)
			{
				set(12, 100.0 * (q2 / deltaQ2)); set(13, 100.0 * (largestQ2 / deltaQ2));
			}
			else
				for (std::size_t i = 12; i < 14; ++i) row.reasons[i] = "Potrzebne skończone q_k i dodatnie ΔQ wyniku fononowego.";
			// scripts/python/examples/ccd_analysis.py:254-271 (phonon_mode), q_k^2-weighted RMS frequency.
			if (valid && strongest && q2 > 0.0 && std::isfinite(q2) && std::isfinite(weighted)) set(4, std::sqrt(weighted / q2));
			else row.reasons[4] = "Potrzebne skończone dodatnie częstotliwości nietranslacyjne i niezerowe przesunięcie q_k.";
			if (strongest)
			{
				const auto &mode = result.modes[*strongest];
				set(5, mode.frequencyMeV); set(6, mode.huangRhys); set(7, mode.effectiveAtomCount);
			}
			else
				for (std::size_t i = 5; i < 8; ++i) row.reasons[i] = "Brak modów nietranslacyjnych z dostępnym S_k.";
		}
	}

	std::vector<PhononComparisonRow> BuildPhononComparisonRows(PhononPanelSession &session, const Path &project)
	{
		std::vector<PhononComparisonRow> rows;
		const auto add = [&](const Path &directory, const std::string &caseId, const std::string &caseName,
			const std::string &label, std::string_view preferred, bool phonons = true) {
			PhononComparisonRow row;
			row.directory = directory; row.caseId = caseId; row.caseName = caseName; row.label = label.empty() ? directory.filename().Utf8() : label;
			row.compared = session.IsCompared(directory);
			row.active = !caseId.empty() && caseId == session.ActiveCaseId() && session.Result() &&
				PhononPanelSession::SameFilePath(directory, session.Settings().resultDirectory, project);
			row.reasons.fill("Brak wartości w podsumowaniu.");
			std::string error;
			std::optional<std::reference_wrapper<const PhononAnalysisResult>> result;
			if (session.Result() && PhononPanelSession::SameFilePath(directory, session.Settings().resultDirectory, project))
				result = std::cref(*session.Result());
			else if (phonons) result = session.PlotModes(directory, error);
			const bool hasModes = result && !result->get().modes.empty();
			if (!result && phonons)
			{
				std::string summaryError;
				result = session.Spectra(directory, summaryError);
				if (!result) row.reasons.fill(summaryError.empty() ? "Brak podsumowania zestawu." : summaryError);
			}
			if (result) FillComparison(row, result->get(), preferred, hasModes, error);
			FillDisplacement(row, session);
			rows.push_back(std::move(row));
		};
		const auto &cases = session.Cases();
		for (bool active : {true, false})
			for (const auto &item : cases)
				if ((item.id == session.ActiveCaseId()) == active)
				{
					const auto before = rows.size();
					for (const auto &path : item.datasets)
					{
						const auto found = std::find_if(session.Datasets().begin(), session.Datasets().end(), [&](const auto &dataset) {
							return PhononPanelSession::SameFilePath(path, dataset.directory, project);
						});
						if (found != session.Datasets().end() && found->kind == AnalysisDatasetKind::Phonons)
							add(found->directory, item.id, item.name, found->label, item.settings.plZpl);
						else if (found == session.Datasets().end() && PhononPanelSession::SameFilePath(path, item.settings.resultDirectory, project))
							add(path, item.id, item.name, path.filename().Utf8(), item.settings.plZpl);
					}
					if (rows.size() == before && !item.settings.selectedCcdDirectory.Empty())
						add(item.settings.selectedCcdDirectory, item.id, item.name,
							item.settings.selectedCcdDirectory.filename().Utf8(), item.settings.plZpl, false);
				}
		for (const auto &dataset : session.Datasets())
			if (dataset.kind == AnalysisDatasetKind::Phonons && session.IsCompared(dataset.directory) && session.DatasetCaseId(dataset.directory).empty())
				add(dataset.directory, {}, "-", dataset.label, session.Settings().plZpl);
		return rows;
	}

	std::vector<DataTableColumn> PhononComparisonColumns(std::span<const PhononComparisonRow> rows)
	{
		if (rows.empty()) return {};
		std::vector<DataTableColumn> columns = {{"porównaj", "", "", {}, {}}, {"Przypadek", "", "", {}, {}},
			{"Zestaw", "", "", {}, {}}, {"ZPL", "eV", "", {}, {}}, {"ΔQ", "amu^½ Å", "", {}, {}},
			{"S", "", "", {}, {}}, {"DW", "%", "", {}, {}}, {"ℏω_eff", "meV", "", {}, {}},
			{"Najsilniejszy mod", "meV", "", {}, {}}, {"S_k", "", "", {}, {}}, {"n_eff", "", "", {}, {}},
			{"Mody urojone", "", "", {}, {}}};
		for (const auto &row : rows)
		{
			columns[0].values.push_back(row.compared ? 1.0 : 0.0);
			columns[1].text.push_back(row.caseName); columns[2].text.push_back(row.label);
			for (std::size_t i = 0; i < 9; ++i)
				columns[i + 3].values.push_back(row.values[i].value_or(std::numeric_limits<double>::quiet_NaN()));
		}
		return columns;
	}

	std::vector<DataTableColumn> PhononComparisonColumns(std::span<const PhononComparisonRow> rows, double carriedFraction)
	{
		auto columns = PhononComparisonColumns(rows);
		if (rows.empty()) return columns;
		columns.push_back({"ΔQ (CCD)", "amu^½ Å", "", {}, {}});
		columns.push_back({"max |ΔR|", "Å", "", {}, {}});
		columns.push_back({std::format("atomy {:g} % ΔQ²", 100.0 * carriedFraction), "", "", {}, {}});
		columns.push_back({"pokrycie ΔQ² modami", "%", "", {}, {}});
		columns.push_back({"największy udział modu", "%", "", {}, {}});
		for (const auto &row : rows)
			for (std::size_t i = 9; i < row.values.size(); ++i)
				columns[i + 3].values.push_back(row.values[i].value_or(std::numeric_limits<double>::quiet_NaN()));
		return columns;
	}

	PlotData BuildLocalisationPlotData(PhononPanelSession &session, const Path &,
		const std::vector<ProjectRootEntry> &, std::span<const glm::vec4> colors)
	{
		PlotData plot;
		plot.id = "localisation"; plot.title = "Lokalizacja ΔR";
		plot.x = {"r", "Å", 0.0, 1.0}; plot.y = {"\\Delta Q^2(r) / \\Delta Q^2", "", 0.0, 1.0};
		for (const auto &item : ComparedDisplacements(session))
		{
			const auto curve = LocalisationCurve(item.displacement.get());
			if (curve.empty()) continue;
			PlotSeriesData series;
			series.id = "localisation/" + item.id;
			series.label = item.name; series.source = item.source;
			const auto style = session.Settings().seriesStyles.find(series.id);
			if (style != session.Settings().seriesStyles.end()) series.style = style->second;
			for (const auto &[radius, share] : curve)
			{
				series.x.push_back(radius); series.y.push_back(share);
			}
			plot.x.maximum = std::max(plot.x.maximum, series.x.back());
			plot.series.push_back(std::move(series));
		}
		ResolvePlotColors(plot, colors);
		return plot;
	}

	std::vector<DataTableColumn> BuildDisplacementOverlapColumns(PhononPanelSession &session, const Path &project)
	{
		const auto cases = ComparedDisplacements(session);
		if (cases.empty()) return {};
		std::vector<DataTableColumn> columns = {{"Przypadek", "", "Ta sama kolejność atomów; bez dopasowania i obrotu.", {}, {}}};
		for (const auto &item : cases) columns[0].text.push_back(item.name);
		for (const auto &column : cases)
		{
			DataTableColumn data{column.name, "", PlotSourceComment(column.source, project), {}, {}};
			for (const auto &row : cases)
				data.values.push_back(DisplacementOverlap(row.displacement.get(), column.displacement.get()).value_or(
					std::numeric_limits<double>::quiet_NaN()));
			columns.push_back(std::move(data));
		}
		return columns;
	}

}
