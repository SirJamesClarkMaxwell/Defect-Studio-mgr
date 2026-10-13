#include "Core/dspch.hpp"

#include "Domain/Crystal/CellEditing.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include "Domain/Crystal/LatticeBasisExpansion.hpp"

namespace DefectStudio
{
	namespace
	{
		bool IsFinite(const glm::vec3 &value)
		{
			return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
		}

		bool IsUsableCell(const LatticeCell &cell)
		{
			for (const auto &vector : cell.vectors)
			{
				if (!IsFinite(vector) || glm::length(vector) <= 0.0f)
					return false;
			}
			const glm::mat3 normalized(glm::normalize(cell.vectors[0]), glm::normalize(cell.vectors[1]),
				glm::normalize(cell.vectors[2]));
			if (std::abs(glm::determinant(normalized)) < 1e-7f)
				return false;
			const auto inverse = cell.ToInverseMatrix();
			return IsFinite(inverse[0]) && IsFinite(inverse[1]) && IsFinite(inverse[2]);
		}

		StructuredError InvalidCellError()
		{
			return StructuredError{ErrorCategory::Validation, Severity::Error,
				"Komórka jest osobliwa lub zawiera nieprawidłowe wartości.",
				"Cell editing requires finite coordinates and an invertible cell.",
				"Podaj dodatnie długości i kąty tworzące nieosobliwą komórkę.",
				"CellEditing", "domain.cell_edit.invalid_cell"};
		}
	} // namespace

	Result<CrystalStructure> WithCell(const CrystalStructure &structure, const LatticeCell &cell,
		CellEditPositions positions)
	{
		if (!IsUsableCell(cell) || !IsUsableCell(structure.cell))
			return InvalidCellError();

		CrystalStructure result = structure;
		result.cell = cell;
		const auto updateSite = [&](auto &site) {
			if (!IsFinite(site.position) || !IsFinite(site.fractional))
				return false;
			if (positions == CellEditPositions::KeepFractional)
				site.position = result.FractionalToCartesian(site.fractional);
			else
				site.fractional = result.CartesianToFractional(site.position);
			return IsFinite(site.position) && IsFinite(site.fractional);
		};
		for (auto &atom : result.atoms)
			if (!updateSite(atom))
				return InvalidCellError();
		for (auto &vacancy : result.vacancies)
			if (!updateSite(vacancy))
				return InvalidCellError();
		if (result.defectFrame)
		{
			if (positions == CellEditPositions::KeepFractional)
				result.defectFrame->origin = result.FractionalToCartesian(
					structure.CartesianToFractional(structure.defectFrame->origin));
			if (!IsFinite(result.defectFrame->origin))
				return InvalidCellError();
		}
		return result;
	}

	LatticeParameters ParametersOf(const LatticeCell &cell)
	{
		const auto angle = [](const glm::vec3 &first, const glm::vec3 &second) {
			return glm::degrees(std::acos(std::clamp(glm::dot(glm::normalize(first), glm::normalize(second)), -1.0f, 1.0f)));
		};
		return LatticeParameters{glm::length(cell.vectors[0]), glm::length(cell.vectors[1]), glm::length(cell.vectors[2]),
			angle(cell.vectors[1], cell.vectors[2]), angle(cell.vectors[0], cell.vectors[2]),
			angle(cell.vectors[0], cell.vectors[1])};
	}

	LatticeCell CellFromParameters(const LatticeCell &current, const LatticeParameters &parameters)
	{
		const auto validLength = [](float value) { return std::isfinite(value) && value > 0.0f; };
		const auto validAngle = [](float value) { return std::isfinite(value) && value > 0.0f && value < 180.0f; };
		if (!IsUsableCell(current) || !validLength(parameters.a) || !validLength(parameters.b) ||
			!validLength(parameters.c) || !validAngle(parameters.alphaDegrees) ||
			!validAngle(parameters.betaDegrees) || !validAngle(parameters.gammaDegrees))
		{
			LatticeCell invalid;
			invalid.vectors.fill(glm::vec3(std::numeric_limits<float>::quiet_NaN()));
			return invalid;
		}

		const glm::vec3 x = glm::normalize(current.vectors[0]);
		const glm::vec3 y = glm::normalize(current.vectors[1] - glm::dot(current.vectors[1], x) * x);
		glm::vec3 z = glm::cross(x, y);
		if (glm::dot(z, current.vectors[2]) < 0.0f)
			z = -z;
		const glm::mat3 orientation(x, y, z);
		LatticeCell result = BuildLatticeCell(CrystalSystem::Triclinic, parameters);
		for (auto &vector : result.vectors)
			vector = orientation * vector;

		// Preserve untouched a/b exactly when only c or its angles are edited.
		const auto previous = ParametersOf(current);
		if (parameters.a == previous.a)
			result.vectors[0] = current.vectors[0];
		if (parameters.b == previous.b && parameters.gammaDegrees == previous.gammaDegrees)
			result.vectors[1] = current.vectors[1];
		return result;
	}

	CrystalStructure WrapIntoCell(const CrystalStructure &structure, std::vector<std::size_t> &outCoincident)
	{
		CrystalStructure result = structure;
		const auto wrapSite = [&](auto &site) {
			for (int axis = 0; axis < 3; ++axis)
			{
				site.fractional[axis] -= std::floor(site.fractional[axis]);
				if (site.fractional[axis] >= 1.0f)
					site.fractional[axis] = 0.0f;
			}
			site.position = result.FractionalToCartesian(site.fractional);
		};
		for (auto &atom : result.atoms)
			wrapSite(atom);
		for (auto &vacancy : result.vacancies)
			wrapSite(vacancy);
		outCoincident = FindCoincidentAtomIndices(result.atoms);
		return result;
	}

	CrystalStructure AddSublattice(const CrystalStructure &structure, const std::string &species,
		BravaisCenteringPreset centering, const glm::vec3 &offset, std::vector<std::size_t> &outCoincident)
	{
		CrystalStructure result = structure;
		AtomSite seed;
		seed.species = species;
		seed.fractional = offset;
		// ExpandBasisOverLattice wraps the fractional coordinates into [0,1).
		for (AtomSite atom : ExpandBasisOverLattice(std::span<const AtomSite>(&seed, 1), centering))
		{
			atom.index = static_cast<int>(result.atoms.size());
			atom.position = result.FractionalToCartesian(atom.fractional);
			result.atoms.push_back(std::move(atom));
		}
		outCoincident = FindCoincidentAtomIndices(result.atoms);
		return result;
	}

	CrystalStructure EmptyCell(const LatticeCell &cell)
	{
		CrystalStructure structure;
		structure.name = "Nowa komórka";
		structure.cell = cell;
		structure.isPeriodic = true;
		return structure;
	}
} // namespace DefectStudio
