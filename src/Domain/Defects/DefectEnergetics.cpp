#include "Core/dspch.hpp"

#include "Domain/Defects/DefectEnergetics.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <utility>

namespace DefectStudio
{
	namespace
	{
		constexpr double kBoundTolerance = 1e-9;

		double ValueOrZero(const SpeciesEnergies &values, const std::string &species)
		{
			const auto found = values.find(species);
			return found == values.end() ? 0.0 : found->second;
		}

		std::string SingleHostSpecies(const ChemicalPhase &host)
		{
			std::string species;
			for (const auto &[name, count] : host.formula)
			{
				if (count == 0)
					continue;
				if (!species.empty())
					return {};
				species = name;
			}
			return species;
		}

		double PhaseSum(const ChemicalPhase &phase, const SpeciesEnergies &deltaMu)
		{
			double sum = 0.0;
			for (const auto &[species, count] : phase.formula)
				if (count != 0)
					sum += count * ValueOrZero(deltaMu, species);
			return sum;
		}

		struct Bound
		{
			std::string name;
			std::vector<double> coefficients;
			double limit = 0.0;
		};

		std::string MakePhaseBound(const ChemicalPhase &phase, const ChemicalPotentialSystem &system, Bound &bound)
		{
			const auto enthalpy = FormationEnthalpy(phase, system.references);
			if (!enthalpy)
				return "Missing or invalid reference for phase " + phase.name;
			bound = {phase.name, std::vector<double>(system.species.size(), 0.0), *enthalpy};
			bool hasAtoms = false;
			for (const auto &[species, count] : phase.formula)
			{
				if (count < 0)
					return "Invalid formula for phase " + phase.name;
				if (count == 0)
					continue;
				const auto found = std::find(system.species.begin(), system.species.end(), species);
				if (found == system.species.end())
					return "Species " + species + " is absent from the chemical-potential system";
				bound.coefficients[static_cast<std::size_t>(found - system.species.begin())] = count;
				hasAtoms = true;
			}
			return hasAtoms ? std::string{} : "Empty formula for phase " + phase.name;
		}

		std::optional<std::vector<double>> Solve(std::vector<std::vector<double>> matrix)
		{
			const std::size_t size = matrix.size();
			for (std::size_t column = 0; column < size; ++column)
			{
				std::size_t pivot = column;
				for (std::size_t row = column + 1; row < size; ++row)
					if (std::abs(matrix[row][column]) > std::abs(matrix[pivot][column]))
						pivot = row;
				if (std::abs(matrix[pivot][column]) < 1e-12)
					return std::nullopt;
				std::swap(matrix[column], matrix[pivot]);
				for (std::size_t row = column + 1; row < size; ++row)
				{
					const double factor = matrix[row][column] / matrix[column][column];
					for (std::size_t entry = column + 1; entry <= size; ++entry)
						matrix[row][entry] -= factor * matrix[column][entry];
				}
			}
			std::vector<double> solution(size);
			for (std::size_t row = size; row-- > 0;)
			{
				double value = matrix[row][size];
				for (std::size_t column = row + 1; column < size; ++column)
					value -= matrix[row][column] * solution[column];
				solution[row] = value / matrix[row][row];
				if (!std::isfinite(solution[row]))
					return std::nullopt;
			}
			return solution;
		}
	} // namespace

	std::optional<double> FormationEnergy(const DefectChargeState &state,
		const FormationReference &reference, const SpeciesEnergies &mu, double fermiLevel)
	{
		double energy = state.totalEnergy - reference.bulkEnergy +
			state.charge * (reference.vbm + fermiLevel) + state.correction;
		for (const auto &[species, count] : state.addedAtoms)
		{
			if (count == 0)
				continue;
			const auto found = mu.find(species);
			if (found == mu.end() || !std::isfinite(found->second))
				return std::nullopt;
			energy -= count * found->second;
		}
		return std::isfinite(energy) ? std::optional<double>{energy} : std::nullopt;
	}

	std::optional<double> TransitionLevel(double intercept1, int charge1, double intercept2, int charge2)
	{
		if (charge1 == charge2)
			return std::nullopt;
		const double level = (intercept1 - intercept2) / (static_cast<double>(charge2) - charge1);
		return std::isfinite(level) ? std::optional<double>{level} : std::nullopt;
	}

	std::vector<EnvelopeSegment> LowerEnvelope(
		std::span<const double> intercepts, std::span<const int> charges, double gap)
	{
		std::vector<EnvelopeSegment> envelope;
		if (gap <= 0.0 || !std::isfinite(gap) || intercepts.empty() || intercepts.size() != charges.size() ||
			!std::all_of(intercepts.begin(), intercepts.end(), [](double value) { return std::isfinite(value); }))
			return envelope;
		std::size_t line = 0;
		for (std::size_t i = 1; i < intercepts.size(); ++i)
			if (intercepts[i] < intercepts[line] ||
				(intercepts[i] == intercepts[line] && charges[i] < charges[line]))
				line = i;

		long double from = 0.0;
		while (from < gap)
		{
			long double to = gap;
			std::size_t next = line;
			for (std::size_t i = 0; i < intercepts.size(); ++i)
			{
				if (charges[i] >= charges[line])
					continue;
				const long double crossing = (static_cast<long double>(intercepts[i]) - intercepts[line]) /
					(static_cast<long double>(charges[line]) - charges[i]);
				if (crossing < from || crossing > to)
					continue;
				if (crossing < to || charges[i] < charges[next] ||
					(charges[i] == charges[next] && i < next))
				{
					to = crossing;
					next = i;
				}
			}
			if (to - from >= 1e-12L)
			{
				if (!envelope.empty() && envelope.back().line == line)
					envelope.back().to = static_cast<double>(to);
				else
					envelope.push_back({static_cast<double>(from), static_cast<double>(to), line});
			}
			if (to >= gap || next == line)
				break;
			from = to;
			line = next;
		}
		return envelope;
	}

	std::vector<ChargeTransition> EnvelopeTransitions(
		std::span<const EnvelopeSegment> envelope, std::span<const int> charges)
	{
		std::vector<ChargeTransition> transitions;
		for (const auto &segment : envelope)
			if (segment.line >= charges.size())
				return {};
		for (std::size_t i = 1; i < envelope.size(); ++i)
			if (charges[envelope[i - 1].line] != charges[envelope[i].line])
				transitions.push_back({charges[envelope[i - 1].line], charges[envelope[i].line], envelope[i].from});
		return transitions;
	}

	std::optional<double> FormationEnthalpy(const ChemicalPhase &phase, const SpeciesEnergies &references)
	{
		double enthalpy = phase.energyPerFormulaUnit;
		for (const auto &[species, count] : phase.formula)
		{
			const auto found = references.find(species);
			if (found == references.end() || !std::isfinite(found->second))
				return std::nullopt;
			enthalpy -= count * found->second;
		}
		return std::isfinite(enthalpy) ? std::optional<double>{enthalpy} : std::nullopt;
	}

	ChemicalPotentialLimits ChemicalPotentialVertices(const ChemicalPotentialSystem &system)
	{
		ChemicalPotentialLimits result;
		const std::size_t size = system.species.size();
		if (size == 0 || size > 6)
			return {{}, "Chemical-potential systems require between 1 and 6 species"};
		for (std::size_t i = 0; i < size; ++i)
		{
			const auto reference = system.references.find(system.species[i]);
			if (reference == system.references.end() || !std::isfinite(reference->second))
				return {{}, "Missing or invalid reference for " + system.species[i]};
			if (system.species[i].empty() ||
				std::find(system.species.begin(), system.species.begin() + i, system.species[i]) != system.species.begin() + i)
				return {{}, "Empty or duplicate species in chemical-potential system"};
		}
		Bound host;
		result.error = MakePhaseBound(system.host, system, host);
		if (!result.error.empty())
			return result;
		std::vector<Bound> bounds;
		const std::string hostSpecies = SingleHostSpecies(system.host);
		for (std::size_t i = 0; i < size; ++i)
		{
			if (system.species[i] == hostSpecies)
				continue;
			Bound element{system.species[i], std::vector<double>(size, 0.0), 0.0};
			element.coefficients[i] = 1.0;
			bounds.push_back(std::move(element));
		}
		for (const auto &phase : system.competing)
		{
			Bound bound;
			result.error = MakePhaseBound(phase, system, bound);
			if (!result.error.empty())
				return result;
			bounds.push_back(std::move(bound));
		}

		const std::size_t dimensions = size - 1;
		std::vector<std::size_t> selected(dimensions);
		std::iota(selected.begin(), selected.end(), std::size_t{0});
		while (true)
		{
			std::vector<std::vector<double>> matrix{host.coefficients};
			matrix.back().push_back(host.limit);
			for (const auto index : selected)
			{
				matrix.push_back(bounds[index].coefficients);
				matrix.back().push_back(bounds[index].limit);
			}
			const auto solution = Solve(std::move(matrix));
			if (solution)
			{
				ChemicalPotentialVertex vertex;
				bool allowed = true;
				for (const auto &bound : bounds)
				{
					const double difference = std::inner_product(bound.coefficients.begin(), bound.coefficients.end(),
						solution->begin(), 0.0) - bound.limit;
					if (!std::isfinite(difference) || difference > kBoundTolerance)
					{
						allowed = false;
						break;
					}
					if (std::abs(difference) <= kBoundTolerance)
						vertex.limitingPhases.push_back(bound.name);
				}
				if (allowed)
				{
					for (std::size_t i = 0; i < size; ++i)
						vertex.deltaMu.emplace(system.species[i], (*solution)[i]);
					const bool duplicate = std::any_of(result.vertices.begin(), result.vertices.end(), [&](const auto &existing) {
						for (const auto &[species, value] : vertex.deltaMu)
							if (std::abs(existing.deltaMu.at(species) - value) > kBoundTolerance)
								return false;
						return true;
					});
					if (!duplicate)
						result.vertices.push_back(std::move(vertex));
				}
			}
			std::size_t position = dimensions;
			while (position > 0 && selected[position - 1] == bounds.size() - dimensions + position - 1)
				--position;
			if (position == 0)
				break;
			++selected[position - 1];
			for (std::size_t i = position; i < dimensions; ++i)
				selected[i] = selected[i - 1] + 1;
		}
		std::sort(result.vertices.begin(), result.vertices.end(), [](const auto &a, const auto &b) {
			return a.limitingPhases < b.limitingPhases;
		});
		if (result.vertices.empty())
			result.error = "no allowed region";
		return result;
	}

	std::vector<std::string> ViolatedChemicalPotentialBounds(
		const ChemicalPotentialSystem &system, const SpeciesEnergies &deltaMu, double tolerance)
	{
		std::vector<std::string> violated;
		const auto hostEnthalpy = FormationEnthalpy(system.host, system.references);
		const double hostSum = PhaseSum(system.host, deltaMu);
		if (!hostEnthalpy || !std::isfinite(hostSum) || std::abs(hostSum - *hostEnthalpy) > tolerance)
			violated.push_back(system.host.name);
		const std::string hostSpecies = SingleHostSpecies(system.host);
		for (const auto &species : system.species)
		{
			if (species == hostSpecies)
				continue;
			const double value = ValueOrZero(deltaMu, species);
			if (!std::isfinite(value) || value > tolerance)
				violated.push_back(species);
		}
		for (const auto &phase : system.competing)
		{
			const auto enthalpy = FormationEnthalpy(phase, system.references);
			const double sum = PhaseSum(phase, deltaMu);
			if (!enthalpy || !std::isfinite(sum) || sum - *enthalpy > tolerance)
				violated.push_back(phase.name);
		}
		return violated;
	}

	SpeciesEnergies AbsoluteChemicalPotentials(const ChemicalPotentialSystem &system, const SpeciesEnergies &deltaMu)
	{
		SpeciesEnergies mu;
		for (const auto &species : system.species)
		{
			const auto reference = system.references.find(species);
			if (reference != system.references.end())
				mu.emplace(species, reference->second + ValueOrZero(deltaMu, species));
		}
		return mu;
	}
} // namespace DefectStudio
