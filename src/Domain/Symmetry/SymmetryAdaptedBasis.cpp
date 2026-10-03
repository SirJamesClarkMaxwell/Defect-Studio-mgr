#include "Core/dspch.hpp"
#include "Domain/Symmetry/SymmetryAdaptedBasis.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <set>
#include <tuple>

namespace DefectStudio
{
	namespace
	{
		[[nodiscard]] StructuredError SalcError(const char *code, const std::string &message)
		{
			return StructuredError{ErrorCategory::Validation, Severity::Error, message, message,
				"Recalculate the symmetry-adapted basis.", "Domain/Symmetry/SymmetryAdaptedBasis", code};
		}
	}

	Result<void> ValidateSymmetryAdaptedBasis(const PointGroupReduction &reduction)
	{
		if (reduction.projectedVectors.empty() && reduction.realPairVectors.empty())
			return {};
		const auto findIrrep = [&](const std::string &label) {
			return std::find_if(reduction.decomposition.begin(), reduction.decomposition.end(),
				[&](const auto &entry) { return entry.irrepLabel == label; });
		};
		using Key = std::tuple<std::string, int, int>;
		std::vector<Key> keys;
		for (const auto &vector : reduction.projectedVectors)
		{
			if (findIrrep(vector.irrepLabel) == reduction.decomposition.end())
				return SalcError("symmetry.salc.unknown_irrep", "A projected vector has an unknown irrep.");
			keys.emplace_back(vector.irrepLabel, vector.occurrenceIndex, vector.irrepRow);
		}
		std::vector<Key> expected;
		for (const auto &irrep : reduction.decomposition)
		{
			if (irrep.multiplicity < 0 || irrep.dimension <= 0)
				return SalcError("symmetry.salc.incomplete_copy", "Invalid irrep multiplicity or dimension.");
			if (expected.size() + static_cast<std::size_t>(irrep.multiplicity) * irrep.dimension > keys.size())
				return SalcError("symmetry.salc.incomplete_copy", "Projected copies have missing rows.");
			for (int occurrence = 0; occurrence < irrep.multiplicity; ++occurrence)
				for (int row = 0; row < irrep.dimension; ++row)
					expected.emplace_back(irrep.irrepLabel, occurrence, row);
		}
		if (keys.size() != expected.size() || std::set<Key>(keys.begin(), keys.end()) !=
			std::set<Key>(expected.begin(), expected.end()))
			return SalcError("symmetry.salc.incomplete_copy", "Projected copies have missing or duplicate rows.");
		if (keys != expected)
			return SalcError("symmetry.salc.order", "Projected vectors are not ordered by irrep, occurrence, row.");
		for (const auto &vectors : {std::cref(reduction.projectedVectors), std::cref(reduction.realPairVectors)})
			for (const auto &vector : vectors.get())
				if (vector.coefficients.size() != reduction.siteLabels.size())
					return SalcError("symmetry.salc.coefficient_count", "A vector does not match the site count.");

		using Pair = std::pair<std::string, std::string>;
		using Component = std::pair<int, int>;
		std::map<Pair, std::set<Component>> components;
		for (const auto &vector : reduction.realPairVectors)
		{
			const auto irrep = findIrrep(vector.irrepLabel);
			const auto conjugate = findIrrep(vector.conjugateIrrepLabel);
			if (vector.conjugateIrrepLabel.empty() || irrep == reduction.decomposition.end() ||
				conjugate == reduction.decomposition.end() || irrep == conjugate ||
				irrep->multiplicity != conjugate->multiplicity || vector.occurrenceIndex < 0 ||
				vector.occurrenceIndex >= irrep->multiplicity || vector.irrepRow < 0 || vector.irrepRow > 1)
				return SalcError("symmetry.salc.real_pair", "Invalid real-pair labels, multiplicity or component.");
			for (const auto &coefficient : vector.coefficients)
				if (!(std::abs(coefficient.numericImaginary) < 1e-12))
					return SalcError("symmetry.salc.real_pair", "A real-pair coefficient is not real.");
			if (!components[{vector.irrepLabel, vector.conjugateIrrepLabel}]
				.emplace(vector.occurrenceIndex, vector.irrepRow).second)
				return SalcError("symmetry.salc.real_pair", "A real pair has a duplicate component.");
		}
		for (const auto &[pair, rows] : components)
			if (rows.size() != static_cast<std::size_t>(findIrrep(pair.first)->multiplicity) * 2)
				return SalcError("symmetry.salc.real_pair", "A real pair has missing components.");
		return {};
	}
} // namespace DefectStudio
