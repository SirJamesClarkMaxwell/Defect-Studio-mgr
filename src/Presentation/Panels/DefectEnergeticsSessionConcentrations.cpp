#include "Core/dspch.hpp"

#include "Presentation/Panels/DefectEnergeticsSession.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <limits>
#include <map>

#include "Core/JobSystem/JobContext.hpp"
#include "Core/JobSystem/JobSystem.hpp"

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

	void DefectEnergeticsSession::StartConcentrations()
	{
		if (m_ConcentrationJob)
			return;
		if (m_ModelDirty)
			rebuild();
		m_Concentrations = {};
		m_ConcentrationsCurrent = false;
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
		frame.gap = m_Model.reference.gap;
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
		const auto findSet = [&](const std::string &name) {
			return std::ranges::find_if(m_Model.muSets, [&](const EnergeticsMuSetView &set) { return set.name == name; });
		};
		const auto from = findSet(settings.lineFrom);
		const auto to = findSet(settings.lineTo);
		std::vector<SpeciesEnergies> mu;
		if (from != m_Model.muSets.end() && to != m_Model.muSets.end() && from != to && settings.linePoints >= 2)
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
		m_SubmittedRevision = m_InputRevision;
		m_ConcentrationJobId = jobSystem->Submit(m_ConcentrationJob, JobPriority::Normal);
	}

	void DefectEnergeticsSession::pollConcentrations()
	{
		if (!m_ConcentrationJob)
			return;
		const auto jobSystem = m_JobSystem.lock();
		const auto snapshot = jobSystem ? jobSystem->GetJob(m_ConcentrationJobId) : std::nullopt;
		if (snapshot && (snapshot->status == JobStatus::Queued || snapshot->status == JobStatus::Running))
			return;
		if (m_SubmittedRevision != m_InputRevision)
		{
			m_ConcentrationJob.reset();
			m_ConcentrationJobId = 0;
			return;
		}
		if (snapshot && snapshot->status == JobStatus::Completed && m_ConcentrationJob->Result())
		{
			m_Concentrations = std::move(*m_ConcentrationJob->Result());
			m_ConcentrationsCurrent = m_Concentrations.error.empty();
		}
		else
			m_Concentrations.error = snapshot && snapshot->status == JobStatus::Cancelled ? "Przerwano."
				: snapshot && !snapshot->errorMessage.empty() ? snapshot->errorMessage : "Obliczenie nie powiodło się.";
		m_ConcentrationJob.reset();
		m_ConcentrationJobId = 0;
	}

	// One row per (point, defect, charge state); intrinsic points keep one row with empty defect fields.
	std::string DefectEnergeticsSession::concentrationsTsv() const
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
