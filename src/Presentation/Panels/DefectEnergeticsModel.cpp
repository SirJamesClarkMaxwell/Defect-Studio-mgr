#include "Core/dspch.hpp"

#include "Presentation/Panels/DefectEnergeticsModel.hpp"

#include <algorithm>
#include <cctype>
#include <format>
#include <map>
#include <numeric>
#include <set>

namespace DefectStudio
{
	namespace
	{
		std::optional<std::size_t> FindRun(const EnergeticsFile &file, const Path &directory)
		{
			for (std::size_t i = 0; i < file.runs.size(); ++i)
				if (file.runs[i].directory == directory)
					return i;
			return std::nullopt;
		}

		const EnergeticsChoice *FindChoice(const EnergeticsFile &file, const std::string &defect, int charge)
		{
			for (const auto &choice : file.choices)
				if (choice.defect == defect && choice.charge == charge)
					return &choice;
			return nullptr;
		}

		int AtomCount(const SpeciesCounts &composition)
		{
			int total = 0;
			for (const auto &[species, count] : composition)
				total += count;
			return total;
		}

		// Energy per formula unit of `run` for `formula`, nullopt when the run is not a whole number of
		// formula units or has no TOTEN.
		std::optional<double> EnergyPerFormulaUnit(const VaspRunRecord &run, const SpeciesCounts &formula)
		{
			const SpeciesCounts composition = CalculationComposition(run);
			if (!run.totalEnergy || composition.empty() || formula.empty())
				return std::nullopt;
			std::optional<int> units;
			for (const auto &[species, count] : formula)
			{
				const auto found = composition.find(species);
				if (count <= 0 || found == composition.end() || found->second % count != 0)
					return std::nullopt;
				if (units && *units != found->second / count)
					return std::nullopt;
				units = found->second / count;
			}
			if (composition.size() != formula.size() || !units || *units <= 0)
				return std::nullopt;
			return *run.totalEnergy / *units;
		}

		std::string Number(double value)
		{
			return std::format("{:.10g}", value);
		}

		std::string Number(const std::optional<double> &value)
		{
			return value ? Number(*value) : std::string();
		}

		std::string ChargeLabel(int charge)
		{
			return charge > 0 ? std::format("+{}", charge) : std::format("{}", charge);
		}

		std::string SafeName(const std::string &name)
		{
			std::string safe;
			for (std::size_t i = 0; i < name.size();)
			{
				const unsigned char c = static_cast<unsigned char>(name[i]);
				const std::size_t length = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : 4;
				safe += length == 1 && (std::isalnum(c) || c == '_' || c == '-') ? static_cast<char>(c) : '_';
				i += length;
			}
			return safe;
		}
	} // namespace

	EnergeticsModel BuildEnergeticsModel(const EnergeticsFile &file, const FnvCorrectionsFile &fnv,
		const std::optional<VaspRunRecordIO::BandEdges> &bulkEdges)
	{
		EnergeticsModel model;
		model.bulkCandidates = BulkCandidates(file.runs);
		if (file.bulkRun)
			model.bulk = FindRun(file, *file.bulkRun);
		else if (model.bulkCandidates.size() == 1)
			model.bulk = model.bulkCandidates.front();
		model.edges = bulkEdges;
		if (model.bulk && file.runs[*model.bulk].totalEnergy)
			model.reference.bulkEnergy = *file.runs[*model.bulk].totalEnergy;
		if (bulkEdges)
		{
			model.reference.vbm = bulkEdges->vbm;
			model.reference.gap = bulkEdges->cbm - bulkEdges->vbm;
		}
		if (file.gap)
			model.reference.gap = *file.gap;
		if (!model.bulk)
		{
			model.chemicalPotentialError = "brak bulku";
			return model;
		}
		const VaspRunRecord &bulk = file.runs[*model.bulk];

		// Rows.
		for (const DefectRunGroup &group : GroupDefectRuns(file.runs, *model.bulk, file.root))
		{
			EnergeticsRow row;
			row.defect = group.defect;
			row.charge = group.charge;
			row.addedAtoms = group.addedAtoms;
			row.candidates = group.runs;
			const RunProposal proposal = ProposeEnergyRun(file.runs, group, *model.bulk);
			row.proposalReason = proposal.reason;
			row.energyRun = proposal.run;
			const EnergeticsChoice *choice = FindChoice(file, row.defect, row.charge);
			if (choice && choice->energyRun)
				if (const auto chosen = FindRun(file, *choice->energyRun))
				{
					row.energyRun = chosen;
					row.userChoice = true;
				}
			if (choice)
				row.included = choice->included;
			if (choice && choice->locpot)
				row.locpot = *choice->locpot;
			else if (row.energyRun && file.runs[*row.energyRun].hasLocpot)
				row.locpot = file.runs[*row.energyRun].directory / Path::FromUtf8("LOCPOT");

			if (row.energyRun)
			{
				const VaspRunRecord &run = file.runs[*row.energyRun];
				if (!CalculationUsable(run))
					row.problems.push_back("obliczenie niezbieżne lub przerwane");
				for (const auto &mismatch : MethodMismatches(run.method, bulk.method))
					row.problems.push_back("metoda: " + mismatch);
			}
			else
				row.problems.push_back("brak wybranego obliczenia: " + proposal.reason);

			if (!row.locpot.Empty())
				for (std::size_t i = 0; i < fnv.corrections.size(); ++i)
					if (fnv.corrections[i].defectLocpot.Native().lexically_normal() == row.locpot.Native().lexically_normal())
					{
						row.fnvEntry = i;
						break;
					}
			if (row.fnvEntry)
			{
				const PersistedFnvCorrection &entry = fnv.corrections[*row.fnvEntry];
				if (entry.charge != row.charge)
					row.problems.push_back(std::format("FNV: q = {}, wiersz q = {}", ChargeLabel(entry.charge), ChargeLabel(row.charge)));
				else if (entry.profiles)
				{
					for (int a = 0; a < 3; ++a)
						row.alignments[a] = AlignFnvInWindow(entry.profiles->axes[a],
							FnvModelPotential(*entry.profiles, a, entry.convention), entry.windows[a]);
					row.correction = CombineFnvCorrection(*entry.profiles, row.alignments, entry.useAxis);
				}
			}
			if (row.charge != 0 && !row.correction)
				row.problems.push_back("brak E_corr (FNV)");
			model.rows.push_back(std::move(row));
		}

		// Chemical potentials.
		ChemicalPotentialSystem &system = model.chemicalPotentials;
		const SpeciesCounts bulkComposition = CalculationComposition(bulk);
		for (const auto &[species, count] : bulkComposition)
			system.species.push_back(species);
		std::set<std::string> others;
		for (const auto &row : model.rows)
			for (const auto &[species, count] : row.addedAtoms)
				if (count != 0 && !bulkComposition.contains(species))
					others.insert(species);
		for (const auto &phase : file.competing)
			for (const auto &[species, count] : phase.formula)
				if (!bulkComposition.contains(species))
					others.insert(species);
		system.species.insert(system.species.end(), others.begin(), others.end());

		int divisor = 0;
		for (const auto &[species, count] : bulkComposition)
			divisor = std::gcd(divisor, count);
		std::string hostName;
		for (const auto &[species, count] : bulkComposition)
		{
			system.host.formula[species] = count / std::max(divisor, 1);
			hostName += species + (count / std::max(divisor, 1) == 1 ? "" : std::to_string(count / std::max(divisor, 1)));
		}
		system.host.name = hostName;
		if (bulk.totalEnergy && divisor > 0)
			system.host.energyPerFormulaUnit = *bulk.totalEnergy / divisor;

		std::vector<std::string> missing;
		for (const auto &species : system.species)
		{
			std::optional<std::size_t> run;
			if (const auto chosen = file.references.find(species); chosen != file.references.end())
				run = FindRun(file, chosen->second);
			else if (bulkComposition.size() == 1 && bulkComposition.contains(species))
				run = model.bulk;
			else
			{
				std::vector<std::size_t> candidates;
				for (std::size_t i = 0; i < file.runs.size(); ++i)
				{
					const SpeciesCounts composition = CalculationComposition(file.runs[i]);
					if (composition.size() == 1 && composition.contains(species) && CalculationUsable(file.runs[i]) &&
						MethodMismatches(file.runs[i].method, bulk.method).empty())
						candidates.push_back(i);
				}
				if (candidates.size() == 1)
					run = candidates.front();
			}
			const auto energy = run ? EnergyPerFormulaUnit(file.runs[*run], {{species, 1}}) : std::nullopt;
			if (energy)
				system.references[species] = *energy;
			else
				missing.push_back(species);
		}
		for (const auto &phase : file.competing)
		{
			const auto run = FindRun(file, phase.run);
			const auto energy = run ? EnergyPerFormulaUnit(file.runs[*run], phase.formula) : std::nullopt;
			if (!energy)
			{
				model.chemicalPotentialError += (model.chemicalPotentialError.empty() ? "" : "; ") +
					std::format("faza {}: brak pasującego obliczenia", phase.name);
				continue;
			}
			system.competing.push_back({phase.name, phase.formula, *energy});
		}
		if (!missing.empty())
		{
			std::string list;
			for (const auto &species : missing)
				list += (list.empty() ? "" : ", ") + species;
			model.chemicalPotentialError += (model.chemicalPotentialError.empty() ? "" : "; ") +
				std::string("brak referencji μ: ") + list + " (wybierz obliczenie)";
			return model;
		}
		const ChemicalPotentialLimits limits = ChemicalPotentialVertices(system);
		if (!limits.error.empty())
			model.chemicalPotentialError += (model.chemicalPotentialError.empty() ? "" : "; ") + limits.error;
		for (const auto &vertex : limits.vertices)
		{
			EnergeticsMuSetView view;
			for (const auto &name : vertex.limitingPhases)
				view.name += (view.name.empty() ? "" : ", ") + name;
			view.deltaMu = vertex.deltaMu;
			view.vertex = true;
			model.muSets.push_back(std::move(view));
		}
		for (const auto &set : file.muSets)
		{
			EnergeticsMuSetView view;
			view.name = set.name;
			view.deltaMu = set.deltaMu;
			view.violated = ViolatedChemicalPotentialBounds(system, set.deltaMu);
			model.muSets.push_back(std::move(view));
		}
		return model;
	}

	std::optional<DefectChargeState> EnergeticsRowState(const EnergeticsFile &file, const EnergeticsRow &row)
	{
		if (!row.energyRun || *row.energyRun >= file.runs.size() || !file.runs[*row.energyRun].totalEnergy)
			return std::nullopt;
		if (row.charge != 0 && !row.correction)
			return std::nullopt;
		return DefectChargeState{row.defect, row.charge, *file.runs[*row.energyRun].totalEnergy, row.addedAtoms,
			row.charge == 0 ? 0.0 : row.correction->correction};
	}

	std::vector<std::pair<std::string, std::string>> ExportEnergeticsTsv(
		const EnergeticsModel &model, const EnergeticsFile &file, const FnvCorrectionsFile &fnv, const double step)
	{
		std::vector<std::pair<std::string, std::string>> files;
		const double gap = model.reference.gap;
		std::vector<const EnergeticsRow *> rows;
		std::vector<std::optional<DefectChargeState>> states;
		for (const auto &row : model.rows)
			if (row.included)
			{
				rows.push_back(&row);
				states.push_back(EnergeticsRowState(file, row));
			}

		// states.tsv
		std::string text = "mu_set\tdefect\tq\tenergy_run\tlocpot\tE_tot (eV)\tE_bulk (eV)\tE_VBM (eV)\tE_g (eV)\t"
			"centre_f1\tcentre_f2\tcentre_f3\tepsilon\tconvention\tE_lat (eV)\tC (eV)\tC_spread (eV)\tE_corr (eV)";
		for (const char *axis : {"a1", "a2", "a3"})
			text += std::format("\tC_{0} (eV)\tsigma_{0} (eV)\tslope_{0}\tuse_{0}\twindow_centre_{0} (A)\twindow_width_{0} (A)", axis);
		text += "\tE_f(E_F=0) (eV)\tE_f(E_F=E_g) (eV)\tproblems\n";
		for (const auto &set : model.muSets)
		{
			const SpeciesEnergies mu = AbsoluteChemicalPotentials(model.chemicalPotentials, set.deltaMu);
			for (std::size_t r = 0; r < rows.size(); ++r)
			{
				const EnergeticsRow &row = *rows[r];
				const PersistedFnvCorrection *entry = row.fnvEntry ? &fnv.corrections[*row.fnvEntry] : nullptr;
				std::vector<std::string> cells{set.name, row.defect, std::to_string(row.charge),
					row.energyRun ? file.runs[*row.energyRun].directory.Utf8() : "", row.locpot.Utf8(),
					row.energyRun ? Number(file.runs[*row.energyRun].totalEnergy) : "",
					Number(model.reference.bulkEnergy), Number(model.reference.vbm), Number(gap)};
				for (int a = 0; a < 3; ++a)
					cells.push_back(entry ? Number(entry->centreFractional[a]) : "");
				cells.push_back(entry ? Number(entry->dielectric) : "");
				cells.push_back(entry ? (entry->convention == FnvModelConvention::Sxdefectalign ? "sxdefectalign" : "pymatgen") : "");
				const auto &correction = row.correction;
				cells.push_back(correction ? Number(correction->latticeEnergy) : "");
				cells.push_back(correction ? Number(correction->alignment) : "");
				cells.push_back(correction ? Number(correction->alignmentSpread) : "");
				cells.push_back(row.charge == 0 ? "0" : correction ? Number(correction->correction) : "");
				for (int a = 0; a < 3; ++a)
				{
					const auto &alignment = row.alignments[a];
					cells.push_back(correction ? Number(alignment.alignment) : "");
					cells.push_back(correction ? Number(alignment.spread) : "");
					cells.push_back(correction ? Number(alignment.slope) : "");
					cells.push_back(entry ? (entry->useAxis[a] ? "1" : "0") : "");
					cells.push_back(correction ? Number(alignment.window.centre) : "");
					cells.push_back(correction ? Number(alignment.window.width) : "");
				}
				const auto &state = states[r];
				cells.push_back(state ? Number(FormationEnergy(*state, model.reference, mu, 0.0)) : "");
				cells.push_back(state ? Number(FormationEnergy(*state, model.reference, mu, gap)) : "");
				std::string problems;
				for (const auto &problem : row.problems)
					problems += (problems.empty() ? "" : "; ") + problem;
				cells.push_back(problems);
				for (std::size_t c = 0; c < cells.size(); ++c)
				{
					std::replace(cells[c].begin(), cells[c].end(), '\t', ' ');
					text += (c ? "\t" : "") + cells[c];
				}
				text += "\n";
			}
		}
		files.emplace_back("states.tsv", std::move(text));

		// Intercepts with mu = 0 for every species: mu cancels inside one defect, so the levels are exact.
		std::map<std::string, std::vector<std::size_t>> byDefect; // defect -> indices into rows/states
		std::vector<std::string> defectOrder;
		for (std::size_t r = 0; r < rows.size(); ++r)
			if (states[r])
			{
				if (!byDefect.contains(rows[r]->defect))
					defectOrder.push_back(rows[r]->defect);
				byDefect[rows[r]->defect].push_back(r);
			}
		const auto lines = [&](const std::string &defect, const SpeciesEnergies &mu) {
			std::pair<std::vector<double>, std::vector<int>> result;
			for (const std::size_t r : byDefect[defect])
			{
				result.first.push_back(FormationEnergy(*states[r], model.reference, mu, 0.0).value_or(0.0));
				result.second.push_back(states[r]->charge);
			}
			return result;
		};
		text = "defect\tq_from\tq_to\tlevel (eV)\n";
		for (const auto &defect : defectOrder)
		{
			SpeciesEnergies zero;
			for (const std::size_t r : byDefect[defect])
				for (const auto &[species, count] : states[r]->addedAtoms)
					zero[species] = 0.0;
			const auto [intercepts, charges] = lines(defect, zero);
			for (const auto &level : EnvelopeTransitions(LowerEnvelope(intercepts, charges, gap), charges))
				text += std::format("{}\t{}\t{}\t{}\n", defect, level.chargeFrom, level.chargeTo, Number(level.level));
		}
		files.emplace_back("transitions.tsv", std::move(text));

		text = "mu_set\tvertex\tspecies\tmu_ref (eV)\tdelta_mu (eV)\tmu (eV)\tviolated\n";
		for (const auto &set : model.muSets)
		{
			std::string violated;
			for (const auto &name : set.violated)
				violated += (violated.empty() ? "" : ", ") + name;
			for (const auto &species : model.chemicalPotentials.species)
			{
				const auto reference = model.chemicalPotentials.references.find(species);
				const auto delta = set.deltaMu.find(species);
				const double deltaValue = delta == set.deltaMu.end() ? 0.0 : delta->second;
				const bool known = reference != model.chemicalPotentials.references.end();
				text += std::format("{}\t{}\t{}\t{}\t{}\t{}\t{}\n", set.name, set.vertex ? 1 : 0, species,
					known ? Number(reference->second) : "", Number(deltaValue),
					known ? Number(reference->second + deltaValue) : "", violated);
			}
		}
		files.emplace_back("mu_sets.tsv", std::move(text));

		for (std::size_t s = 0; s < model.muSets.size(); ++s)
		{
			const auto &set = model.muSets[s];
			const SpeciesEnergies mu = AbsoluteChemicalPotentials(model.chemicalPotentials, set.deltaMu);
			text = "E_F (eV)";
			for (const auto &defect : defectOrder)
				for (const std::size_t r : byDefect[defect])
					text += std::format("\t{} q={} (eV)", defect, ChargeLabel(rows[r]->charge));
			for (const auto &defect : defectOrder)
				text += std::format("\t{} envelope (eV)", defect);
			text += "\n";
			std::vector<double> grid;
			for (int k = 0; step > 0.0 && k * step < gap - 1e-9; ++k)
				grid.push_back(k * step);
			if (gap > 0.0)
				grid.push_back(gap);
			for (const double fermi : grid)
			{
				text += Number(fermi);
				for (const auto &defect : defectOrder)
					for (const std::size_t r : byDefect[defect])
						text += "\t" + Number(FormationEnergy(*states[r], model.reference, mu, fermi));
				for (const auto &defect : defectOrder)
				{
					double lowest = 0.0;
					bool first = true;
					for (const std::size_t r : byDefect[defect])
						if (const auto value = FormationEnergy(*states[r], model.reference, mu, fermi))
						{
							lowest = first ? *value : std::min(lowest, *value);
							first = false;
						}
					text += "\t" + (first ? std::string() : Number(lowest));
				}
				text += "\n";
			}
			files.emplace_back(std::format("formation_{}_{}.tsv", s + 1, SafeName(set.name)), std::move(text));
		}
		return files;
	}
} // namespace DefectStudio
