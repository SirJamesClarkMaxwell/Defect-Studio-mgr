#include "Core/dspch.hpp"

#include "Presentation/Panels/DefectEnergeticsPanel.hpp"
#include "Presentation/Panels/PlotWidgets.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <limits>
#include <map>

#include <imgui.h>
#include <implot.h>

#include "Core/JobSystem/JobContext.hpp"
#include "Core/JobSystem/JobSystem.hpp"
#include "Core/Platform/FileDialog.hpp"
#include "IO/TextFileIO.hpp"

namespace DefectStudio
{
	// Solves the neutrality condition at every (lambda, T) point - thousands of Fermi-Dirac integrals
	// times a bisection each, so seconds for a full grid: kept off the UI thread.
	class ConcentrationJob final : public IJob
	{
	public:
		ConcentrationJob(std::vector<ConcentrationDefectInput> inputs, FormationReference reference,
			EffectiveMassBands bands, std::vector<SpeciesEnergies> mu, double fixedCharge, EnergeticsConcentrations frame)
			: m_Inputs(std::move(inputs)), m_Reference(reference), m_Bands(bands), m_Mu(std::move(mu)),
			  m_FixedCharge(fixedCharge), m_Frame(std::move(frame)) {}

		[[nodiscard]] std::string GetName() const override { return "Koncentracje defektów"; }
		[[nodiscard]] std::string GetType() const override { return "energetics.concentrations"; }
		void Execute(JobContext &context) override
		{
			EnergeticsConcentrations result = m_Frame;
			const std::size_t temperatures = result.temperatures.size();
			for (std::size_t l = 0; l < m_Mu.size(); ++l)
			{
				ConcentrationDefects defects = BuildConcentrationDefects(m_Inputs, m_Reference, m_Mu[l]);
				if (!defects.error.empty())
				{
					result.error = defects.error;
					break;
				}
				for (std::size_t t = 0; t < temperatures; ++t)
				{
					context.ThrowIfCancellationRequested();
					context.SetProgress(static_cast<float>(l * temperatures + t), static_cast<float>(m_Mu.size() * temperatures));
					result.solutions.push_back(SolveDefectConcentrations(defects.defects, m_Bands, m_Reference.gap,
						result.temperatures[t], m_FixedCharge));
				}
				result.intercepts.push_back(std::move(defects));
			}
			m_Result = std::move(result);
		}
		[[nodiscard]] std::optional<EnergeticsConcentrations> &Result() noexcept { return m_Result; }

	private:
		std::vector<ConcentrationDefectInput> m_Inputs;
		FormationReference m_Reference;
		EffectiveMassBands m_Bands;
		std::vector<SpeciesEnergies> m_Mu;
		double m_FixedCharge = 0.0;
		EnergeticsConcentrations m_Frame; // names and axes, filled in by Execute
		std::optional<EnergeticsConcentrations> m_Result;
	};

	namespace
	{
		constexpr ImVec4 kWarningColor(0.95f, 0.75f, 0.30f, 1.0f);
		constexpr double kNan = std::numeric_limits<double>::quiet_NaN();

		// A density for a log axis: NaN (a gap in the line) when unconverged or zero.
		double LogPlottable(const DefectConcentrationSolution &solution, double value)
		{
			return solution.converged && value > 0.0 ? value : kNan;
		}
	} // namespace

	void DefectEnergeticsPanel::startConcentrations()
	{
		m_Concentrations = {};
		const ConcentrationSettings &settings = m_File->concentrations;
		if (m_Model.muSets.empty() || !m_Model.bulk || m_Model.reference.gap <= 0.0)
		{
			m_Concentrations.error = "Potrzebne: bulk z przerwą i potencjały chemiczne.";
			return;
		}

		// Defects: the included states with an energy (and E_corr when charged), by defect.
		int bulkAtoms = 0;
		for (const int ions : m_File->runs[*m_Model.bulk].ions)
			bulkAtoms += ions;
		const double defaultSites = m_BulkVolume ? bulkAtoms / *m_BulkVolume * 1e24 : 0.0;
		std::map<std::string, ConcentrationDefectInput> grouped;
		for (const auto &row : m_Model.rows)
			if (row.included)
				if (const auto state = EnergeticsRowState(*m_File, row))
					grouped[row.defect].chargeStates.push_back(*state);
		EnergeticsConcentrations frame;
		std::vector<ConcentrationDefectInput> inputs;
		for (auto &[defect, input] : grouped)
		{
			const auto sites = settings.sites.find(defect);
			const auto degeneracy = settings.degeneracy.find(defect);
			input.siteDensityCm3 = sites != settings.sites.end() ? sites->second : defaultSites;
			input.degeneracies.assign(input.chargeStates.size(), degeneracy != settings.degeneracy.end() ? degeneracy->second : 1.0);
			if (input.siteDensityCm3 <= 0.0)
			{
				m_Concentrations.error = defect + ": brak gęstości miejsc (bulk bez CONTCAR - wpisz N_sites).";
				return;
			}
			frame.defects.push_back(defect);
			inputs.push_back(std::move(input));
		}
		if (inputs.empty())
		{
			m_Concentrations.error = "Brak stanów z energią (i E_corr dla naładowanych).";
			return;
		}

		// Chemical potentials: the chosen set, or the line between two sets.
		const auto findSet = [&](const std::string &name) -> const EnergeticsMuSetView * {
			for (const auto &set : m_Model.muSets)
				if (set.name == name)
					return &set;
			return nullptr;
		};
		const EnergeticsMuSetView *from = findSet(settings.lineFrom);
		const EnergeticsMuSetView *to = findSet(settings.lineTo);
		std::vector<SpeciesEnergies> mu;
		if (from && to && from != to && settings.linePoints >= 2)
			for (int k = 0; k < settings.linePoints; ++k)
			{
				const double lambda = static_cast<double>(k) / (settings.linePoints - 1);
				SpeciesEnergies delta;
				for (const auto &species : m_Model.chemicalPotentials.species)
				{
					const auto a = from->deltaMu.find(species);
					const auto b = to->deltaMu.find(species);
					delta[species] = (1.0 - lambda) * (a != from->deltaMu.end() ? a->second : 0.0)
						+ lambda * (b != to->deltaMu.end() ? b->second : 0.0);
				}
				frame.lambdas.push_back(lambda);
				frame.muLabels.push_back(std::format("λ = {:.3f} ({} → {})", lambda, from->name, to->name));
				mu.push_back(AbsoluteChemicalPotentials(m_Model.chemicalPotentials, delta));
			}
		else
		{
			const auto &set = m_Model.muSets[static_cast<std::size_t>(m_MuSet)];
			frame.lambdas = {0.0};
			frame.muLabels = {set.name};
			mu.push_back(AbsoluteChemicalPotentials(m_Model.chemicalPotentials, set.deltaMu));
		}

		const int steps = std::max(1, settings.temperatureSteps);
		for (int k = 0; k < steps; ++k)
			frame.temperatures.push_back(steps == 1 ? settings.temperatureMin
				: settings.temperatureMin + (settings.temperatureMax - settings.temperatureMin) * k / (steps - 1));

		const auto jobSystem = m_JobSystem.lock();
		if (!jobSystem)
			return;
		m_ConcentrationJob = CreateRef<ConcentrationJob>(std::move(inputs), m_Model.reference,
			EffectiveMassBands{settings.electronMass, settings.holeMass}, std::move(mu), settings.fixedCharge, std::move(frame));
		m_ConcentrationJobId = jobSystem->Submit(m_ConcentrationJob, JobPriority::Normal);
	}

	void DefectEnergeticsPanel::pollConcentrations()
	{
		if (!m_ConcentrationJob)
			return;
		const auto jobSystem = m_JobSystem.lock();
		const auto snapshot = jobSystem ? jobSystem->GetJob(m_ConcentrationJobId) : std::nullopt;
		if (snapshot && (snapshot->status == JobStatus::Queued || snapshot->status == JobStatus::Running))
			return;
		if (snapshot && snapshot->status == JobStatus::Completed && m_ConcentrationJob->Result())
			m_Concentrations = std::move(*m_ConcentrationJob->Result());
		else
			m_Concentrations.error = snapshot && snapshot->status == JobStatus::Cancelled ? "Przerwano."
				: snapshot && !snapshot->errorMessage.empty() ? snapshot->errorMessage : "Obliczenie nie powiodło się.";
		m_ConcentrationLambda = std::clamp(m_ConcentrationLambda, 0, std::max(0, static_cast<int>(m_Concentrations.lambdas.size()) - 1));
		m_ConcentrationTemperature = std::clamp(m_ConcentrationTemperature, 0, std::max(0, static_cast<int>(m_Concentrations.temperatures.size()) - 1));
		m_ConcentrationJob.reset();
		m_ConcentrationJobId = 0;
	}

	// Inputs (saved in energetics.yaml), the job, and the linked plots: c(T) + E_F(T) at one lambda;
	// along the Delta mu line also c(lambda) + E_F(lambda) at one T and a T x lambda map.
	void DefectEnergeticsPanel::drawConcentrations()
	{
		if (!ImGui::CollapsingHeader("Koncentracje równowagowe"))
			return;
		ConcentrationSettings &settings = m_File->concentrations;
		bool edited = false;
		const float field = 7.0f * ImGui::GetFontSize();

		ImGui::SetNextItemWidth(field);
		edited |= ImGui::InputDouble("m_e* DOS", &settings.electronMass, 0.0, 0.0, "%.3f");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(field);
		edited |= ImGui::InputDouble("m_h* DOS [m_0]", &settings.holeMass, 0.0, 0.0, "%.3f");
		ImGui::SetItemTooltip("Masy efektywne gęstości stanów (wszystkie doliny / pasma, bez spinu).\n"
			"Domyślnie 0.46 / 1.25: Alasio et al., PR Applied 21, 054043 (2024) - sprawdź normalizację dolin.\n"
			"W diamencie nośniki liczą się dopiero blisko krawędzi pasm - E_F zwykle ustalają defekty.");
		ImGui::SetNextItemWidth(field);
		edited |= ImGui::InputDouble("T od", &settings.temperatureMin, 0.0, 0.0, "%.0f K");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(field);
		edited |= ImGui::InputDouble("do", &settings.temperatureMax, 0.0, 0.0, "%.0f K");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(field);
		edited |= ImGui::InputInt("punktów T", &settings.temperatureSteps);
		settings.temperatureMin = std::clamp(settings.temperatureMin, 1.0, 3000.0);
		settings.temperatureMax = std::clamp(settings.temperatureMax, settings.temperatureMin, 3000.0);
		settings.temperatureSteps = std::clamp(settings.temperatureSteps, 1, 1000);
		ImGui::SetNextItemWidth(field);
		edited |= ImGui::InputDouble("N_fixed [cm⁻³]", &settings.fixedCharge, 0.0, 0.0, "%.3e");
		ImGui::SetItemTooltip("Stały zjonizowany ładunek domieszek spoza listy: donory +, akceptory -.");

		// Delta mu line between two sets.
		const auto setCombo = [&](const char *label, std::string &name) {
			ImGui::SetNextItemWidth(2.0f * field);
			if (ImGui::BeginCombo(label, name.empty() ? "brak" : name.c_str()))
			{
				if (ImGui::Selectable("brak", name.empty()))
				{
					name.clear();
					edited = true;
				}
				for (const auto &set : m_Model.muSets)
					if (ImGui::Selectable(set.name.c_str(), set.name == name))
					{
						name = set.name;
						edited = true;
					}
				ImGui::EndCombo();
			}
		};
		setCombo("linia Δμ od", settings.lineFrom);
		ImGui::SameLine();
		setCombo("do##lineTo", settings.lineTo);
		ImGui::SameLine();
		ImGui::SetNextItemWidth(field);
		edited |= ImGui::InputInt("punktów λ", &settings.linePoints);
		settings.linePoints = std::clamp(settings.linePoints, 2, 200);
		ImGui::SetItemTooltip("Bez linii: tylko zaznaczony zestaw μ. Z linią: Δμ(λ) = (1-λ)·od + λ·do, mapa T × λ.");

		// Per defect: site density and degeneracy.
		std::vector<std::string> defects;
		for (const auto &row : m_Model.rows)
			if (row.included && (defects.empty() || defects.back() != row.defect))
				defects.push_back(row.defect);
		int bulkAtoms = 0;
		if (m_Model.bulk)
			for (const int ions : m_File->runs[*m_Model.bulk].ions)
				bulkAtoms += ions;
		const double defaultSites = m_BulkVolume ? bulkAtoms / *m_BulkVolume * 1e24 : 0.0;
		if (ImGui::BeginTable("##ConcentrationDefects", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit))
		{
			ImGui::TableSetupColumn("Defekt");
			ImGui::TableSetupColumn("N_sites [cm⁻³]");
			ImGui::TableSetupColumn("g (orientacje × spin)");
			ImGui::TableHeadersRow();
			for (const auto &defect : defects)
			{
				ImGui::PushID(defect.c_str());
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				ImGui::TextUnformatted(defect.c_str());
				ImGui::TableNextColumn();
				const auto sites = settings.sites.find(defect);
				double value = sites != settings.sites.end() ? sites->second : defaultSites;
				ImGui::SetNextItemWidth(field);
				if (ImGui::InputDouble("##sites", &value, 0.0, 0.0, "%.4e"))
				{
					settings.sites[defect] = value;
					edited = true;
				}
				ImGui::SetItemTooltip("Domyślnie atomy bulku / objętość komórki (%.4e cm⁻³). Kompleksy i międzywęzłowe: wpisz sam.",
					defaultSites);
				ImGui::TableNextColumn();
				const auto degeneracy = settings.degeneracy.find(defect);
				double g = degeneracy != settings.degeneracy.end() ? degeneracy->second : 1.0;
				ImGui::SetNextItemWidth(field);
				if (ImGui::InputDouble("##g", &g, 0.0, 0.0, "%.2f"))
				{
					settings.degeneracy[defect] = g;
					edited = true;
				}
				ImGui::SetItemTooltip("Jedna wartość dla wszystkich stanów ładunkowych defektu (ponytail: bez g na stan).");
				ImGui::PopID();
			}
			ImGui::EndTable();
		}
		if (edited)
			changed();

		if (m_ConcentrationJob)
		{
			const auto jobSystem = m_JobSystem.lock();
			const auto snapshot = jobSystem ? jobSystem->GetJob(m_ConcentrationJobId) : std::nullopt;
			ImGui::ProgressBar(snapshot && snapshot->totalWork > 0.0f ? snapshot->completedWork / snapshot->totalWork : 0.0f,
				ImVec2(-ImGui::CalcTextSize("Anuluj").x - 3.0f * ImGui::GetStyle().FramePadding.x, 0.0f));
			ImGui::SameLine();
			if (ImGui::Button("Anuluj##concentrations") && jobSystem)
				(void)jobSystem->RequestCancel(m_ConcentrationJobId);
		}
		else
		{
			if (ImGui::Button("Policz koncentracje"))
				startConcentrations();
			ImGui::SameLine();
			ImGui::BeginDisabled(m_Concentrations.solutions.empty());
			if (ImGui::Button("Eksport koncentracji TSV..."))
				if (const auto folder = Platform::PickFolder(); folder && folder->has_value())
				{
					std::string error;
					m_Message = TextFileIO::Save(**folder / Path::FromUtf8("concentrations.tsv"), concentrationsTsv(), error)
						? "Zapisano concentrations.tsv w " + (*folder)->Utf8() + "." : "Eksport: " + error;
				}
			ImGui::EndDisabled();
		}
		if (!m_Concentrations.error.empty())
			ImGui::TextColored(kWarningColor, "%s", m_Concentrations.error.c_str());

		const EnergeticsConcentrations &result = m_Concentrations;
		const std::size_t nT = result.temperatures.size();
		const std::size_t nL = result.solutions.size() / std::max<std::size_t>(1, nT);
		if (nL == 0 || nT == 0)
			return;
		std::size_t failed = 0;
		for (const auto &solution : result.solutions)
			failed += solution.converged ? 0 : 1;
		if (failed)
			ImGui::TextColored(kWarningColor, "%zu punktów bez rozwiązania (przerwy na wykresach).", failed);
		const double gap = m_Model.reference.gap;
		const double margin = std::max(0.25, 0.08 * gap);
		const float height = m_ConcentrationHeight;
		const auto solution = [&](std::size_t l, std::size_t t) -> const DefectConcentrationSolution & { return result.solutions[l * nT + t]; };

		// c(T) and E_F(T) at one lambda.
		if (nL > 1)
		{
			ImGui::SetNextItemWidth(-1.0f);
			ImGui::SliderInt("##lambda", &m_ConcentrationLambda, 0, static_cast<int>(nL) - 1,
				result.muLabels[static_cast<std::size_t>(m_ConcentrationLambda)].c_str());
		}
		const std::size_t l = static_cast<std::size_t>(std::clamp(m_ConcentrationLambda, 0, static_cast<int>(nL) - 1));
		const auto plotSeries = [&](const char *name, std::span<const double> xs, auto value, std::size_t count, const ImPlotSpec &spec) {
			std::vector<double> ys(count);
			for (std::size_t i = 0; i < count; ++i)
				ys[i] = value(i);
			ImPlot::PlotLine(name, xs.data(), ys.data(), static_cast<int>(count), spec);
		};
		ImPlotSpec dashed;
		dashed.LineColor = ImVec4(0.7f, 0.7f, 0.7f, 1.0f);
		if (ImPlot::BeginPlot("##ConcentrationT", ImVec2(-1.0f, height)))
		{
			ImPlot::SetupAxes("T [K]", "c [cm⁻³]", ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);
			ImPlot::SetupAxisScale(ImAxis_Y1, ImPlotScale_Log10);
			ImPlot::SetupLegend(ImPlotLocation_SouthEast);
			for (std::size_t d = 0; d < result.defects.size(); ++d)
				plotSeries(result.defects[d].c_str(), result.temperatures,
					[&](std::size_t t) { const auto &s = solution(l, t); return LogPlottable(s, d < s.defectTotalsCm3.size() ? s.defectTotalsCm3[d] : 0.0); },
					nT, ImPlotSpec());
			plotSeries("n", result.temperatures, [&](std::size_t t) { return LogPlottable(solution(l, t), solution(l, t).electronsCm3); }, nT, dashed);
			plotSeries("p", result.temperatures, [&](std::size_t t) { return LogPlottable(solution(l, t), solution(l, t).holesCm3); }, nT, dashed);
			ImPlot::EndPlot();
		}
		PlotHeightHandle("##concentrationHeight", m_ConcentrationHeight);
		if (ImPlot::BeginPlot("##FermiT", ImVec2(-1.0f, 0.6f * height)))
		{
			ImPlot::SetupAxes("T [K]", "E_F - E_VBM [eV]", ImPlotAxisFlags_AutoFit);
			ImPlot::SetupAxisLimits(ImAxis_Y1, -margin, gap + margin, ImPlotCond_Always);
			DrawBandEdges(0.0, gap, false);
			plotSeries("E_F", result.temperatures, [&](std::size_t t) { return solution(l, t).converged ? solution(l, t).fermiLevelEv : kNan; }, nT, ImPlotSpec());
			ImPlot::EndPlot();
		}
		if (nL < 2)
			return;

		// Along the line at one T, and the T x lambda map.
		ImGui::SetNextItemWidth(-1.0f);
		ImGui::SliderInt("##temperature", &m_ConcentrationTemperature, 0, static_cast<int>(nT) - 1,
			std::format("T = {:.0f} K", result.temperatures[static_cast<std::size_t>(std::clamp(m_ConcentrationTemperature, 0, static_cast<int>(nT) - 1))]).c_str());
		const std::size_t t = static_cast<std::size_t>(std::clamp(m_ConcentrationTemperature, 0, static_cast<int>(nT) - 1));
		if (ImPlot::BeginPlot("##ConcentrationLambda", ImVec2(-1.0f, height)))
		{
			ImPlot::SetupAxes("λ", "c [cm⁻³]", ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);
			ImPlot::SetupAxisScale(ImAxis_Y1, ImPlotScale_Log10);
			ImPlot::SetupLegend(ImPlotLocation_SouthEast);
			for (std::size_t d = 0; d < result.defects.size(); ++d)
				plotSeries(result.defects[d].c_str(), result.lambdas,
					[&](std::size_t k) { const auto &s = solution(k, t); return LogPlottable(s, d < s.defectTotalsCm3.size() ? s.defectTotalsCm3[d] : 0.0); },
					nL, ImPlotSpec());
			plotSeries("n", result.lambdas, [&](std::size_t k) { return LogPlottable(solution(k, t), solution(k, t).electronsCm3); }, nL, dashed);
			plotSeries("p", result.lambdas, [&](std::size_t k) { return LogPlottable(solution(k, t), solution(k, t).holesCm3); }, nL, dashed);
			ImPlot::EndPlot();
		}

		std::vector<std::string> mapNames{"E_F - E_VBM [eV]"};
		for (const auto &defect : result.defects)
			mapNames.push_back("log10 c(" + defect + ")");
		m_ConcentrationMap = std::clamp(m_ConcentrationMap, 0, static_cast<int>(mapNames.size()) - 1);
		ImGui::SetNextItemWidth(2.0f * field);
		if (ImGui::BeginCombo("Mapa T × λ", mapNames[static_cast<std::size_t>(m_ConcentrationMap)].c_str()))
		{
			for (int k = 0; k < static_cast<int>(mapNames.size()); ++k)
				if (ImGui::Selectable(mapNames[static_cast<std::size_t>(k)].c_str(), k == m_ConcentrationMap))
					m_ConcentrationMap = k;
			ImGui::EndCombo();
		}
		std::vector<double> values(nL * nT, kNan); // row 0 = top = lambda 1
		double low = std::numeric_limits<double>::max(), high = std::numeric_limits<double>::lowest();
		for (std::size_t k = 0; k < nL; ++k)
			for (std::size_t c = 0; c < nT; ++c)
			{
				const auto &s = solution(k, c);
				const std::size_t d = static_cast<std::size_t>(m_ConcentrationMap - 1);
				const double v = !s.converged ? kNan : m_ConcentrationMap == 0 ? s.fermiLevelEv
					: d < s.defectTotalsCm3.size() && s.defectTotalsCm3[d] > 0.0 ? std::log10(s.defectTotalsCm3[d]) : kNan;
				values[(nL - 1 - k) * nT + c] = v;
				if (std::isfinite(v))
				{
					low = std::min(low, v);
					high = std::max(high, v);
				}
			}
		if (low > high)
			return;
		if (high - low < 1e-12)
			high = low + 1.0;
		const float scaleWidth = 6.0f * ImGui::GetFontSize();
		if (ImPlot::BeginPlot("##ConcentrationMap", ImVec2(ImGui::GetContentRegionAvail().x - scaleWidth - ImGui::GetStyle().ItemSpacing.x, height)))
		{
			ImPlot::SetupAxes("T [K]", "λ", ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);
			ImPlot::PlotHeatmap("##map", values.data(), static_cast<int>(nL), static_cast<int>(nT), low, high, nullptr,
				ImPlotPoint(result.temperatures.front(), 0.0), ImPlotPoint(result.temperatures.back(), 1.0));
			ImPlot::EndPlot();
		}
		ImGui::SameLine();
		ImPlot::ColormapScale("##mapScale", low, high, ImVec2(scaleWidth, height), "%.2f");
	}

	// One row per (point, defect, charge state); intrinsic points keep one row with empty defect fields.
	std::string DefectEnergeticsPanel::concentrationsTsv() const
	{
		const EnergeticsConcentrations &result = m_Concentrations;
		std::string text = "T_K\tlambda\tmu_set\tN_fixed_cm-3\tE_F_from_VBM_eV\tn_cm-3\tp_cm-3\tdefect\tq\tA_q_eV\t"
			"N_sites_cm-3\tg\tc_q_cm-3\tc_total_cm-3\trelative_neutrality_residual\tconverged\terror\n";
		const std::size_t nT = result.temperatures.size();
		for (std::size_t i = 0; i < result.solutions.size() && nT > 0; ++i)
		{
			const std::size_t l = i / nT;
			const DefectConcentrationSolution &s = result.solutions[i];
			const std::string point = std::format("{}\t{}\t{}\t{:.6e}\t{:.6f}\t{:.6e}\t{:.6e}", s.temperatureK, result.lambdas[l],
				result.muLabels[l], s.fixedChargeCm3, s.fermiLevelEv, s.electronsCm3, s.holesCm3);
			const std::string tail = std::format("\t{:.3e}\t{}\t{}\n", s.relativeNeutralityResidual, s.converged ? 1 : 0, s.error);
			if (s.chargeStates.empty() || l >= result.intercepts.size())
			{
				text += point + "\t\t\t\t\t\t\t" + tail;
				continue;
			}
			for (const auto &state : s.chargeStates)
			{
				const ConcentrationDefect &defect = result.intercepts[l].defects[state.defectIndex];
				const ConcentrationChargeState &input = defect.chargeStates[state.chargeStateIndex];
				text += point + std::format("\t{}\t{}\t{:.6f}\t{:.6e}\t{}\t{:.6e}\t{:.6e}", defect.defect, state.charge, input.interceptEv,
					defect.siteDensityCm3, input.degeneracy, state.concentrationCm3,
					state.defectIndex < s.defectTotalsCm3.size() ? s.defectTotalsCm3[state.defectIndex] : 0.0) + tail;
			}
		}
		return text;
	}
} // namespace DefectStudio
