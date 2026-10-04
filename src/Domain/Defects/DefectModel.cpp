#include "Core/dspch.hpp"

#include "Domain/Defects/DefectModel.hpp"

#include <algorithm>
#include <cstddef>
#include <utility>

namespace DefectStudio
{
	namespace
	{
		[[nodiscard]] StructuredError MakeInvalidDefectOperationError(
			std::size_t atomIndex,
			std::size_t atomCount,
			const char *operation)
		{
			return StructuredError{
				ErrorCategory::Validation,
				Severity::Error,
				"Defect operation failed.",
				std::string(operation) + " references atom index " + std::to_string(atomIndex) +
					", but structure has " + std::to_string(atomCount) + " atom(s).",
				"Use a valid atom index from the pristine structure or rebuild the configuration after editing atoms.",
				"DefectModel",
				"domain.defect.invalid_atom_index"};
		}

		void ReindexAtoms(std::vector<AtomSite> &atoms)
		{
			for (std::size_t index = 0; index < atoms.size(); ++index)
				atoms[index].index = static_cast<int>(index);
		}

		void RemoveBondsReferencingAtom(std::vector<Bond> &bonds, std::size_t removedIndex)
		{
			bonds.erase(
				std::remove_if(bonds.begin(), bonds.end(), [removedIndex](Bond &bond) {
					if (bond.firstAtomIndex == removedIndex || bond.secondAtomIndex == removedIndex)
						return true;
					if (bond.firstAtomIndex > removedIndex)
						--bond.firstAtomIndex;
					if (bond.secondAtomIndex > removedIndex)
						--bond.secondAtomIndex;
					return false;
				}),
				bonds.end());
		}
	} // namespace

	VacancySite MakeVacancySite(const CrystalStructure &structure, const glm::vec3 &position, std::string sourceSpecies)
	{
		VacancySite vacancy;
		vacancy.position = position;
		vacancy.fractional = structure.CartesianToFractional(position);
		vacancy.sourceSpecies = std::move(sourceSpecies);
		vacancy.index = -1;
		return vacancy;
	}

	Result<DefectFrame> MakeDefectFrame(
		const glm::vec3 &origin, const glm::vec3 &zTarget, const std::optional<glm::vec3> xTarget)
	{
		const glm::vec3 zAxis = zTarget - origin;
		if (glm::length(zAxis) < 1e-4f)
			return StructuredError{ErrorCategory::Validation, Severity::Error,
				"The defect axis needs two different points.", "MakeDefectFrame: zTarget coincides with origin.",
				"Pick a target that is not the origin.", "DefectModel", "domain.defect_frame.degenerate_axis"};
		DefectFrame frame;
		frame.origin = origin;
		frame.z = glm::normalize(zAxis);
		auto perpendicular = [&](const glm::vec3 &v) { return v - glm::dot(v, frame.z) * frame.z; };
		glm::vec3 x = xTarget ? perpendicular(*xTarget - origin) : glm::vec3(0.0f);
		if (glm::length(x) < 1e-3f)
		{
			const glm::vec3 a = glm::abs(frame.z);
			const glm::vec3 fallback = a.x <= a.y && a.x <= a.z ? glm::vec3(1, 0, 0)
				: a.y <= a.z ? glm::vec3(0, 1, 0) : glm::vec3(0, 0, 1);
			x = perpendicular(fallback);
		}
		frame.x = glm::normalize(x);
		frame.y = glm::cross(frame.z, frame.x);
		return frame;
	}

	Result<void> ApplyVacancy(CrystalStructure &structure, const PointDefectOperation &operation)
	{
		if (operation.atomIndex >= structure.atoms.size())
			return MakeInvalidDefectOperationError(operation.atomIndex, structure.atoms.size(), "Vacancy");

		const AtomSite removedAtom = structure.atoms[operation.atomIndex];
		VacancySite vacancy;
		vacancy.position = removedAtom.position;
		vacancy.fractional = removedAtom.fractional;
		vacancy.sourceSpecies = removedAtom.species;
		vacancy.label = operation.label;
		vacancy.index = removedAtom.index;
		structure.vacancies.push_back(std::move(vacancy));

		structure.atoms.erase(structure.atoms.begin() + static_cast<std::ptrdiff_t>(operation.atomIndex));
		RemoveBondsReferencingAtom(structure.bonds, operation.atomIndex);
		ReindexAtoms(structure.atoms);
		return {};
	}

	Result<void> ApplyInterstitial(CrystalStructure &structure, const PointDefectOperation &operation)
	{
		AtomSite atom = operation.atom;
		atom.index = static_cast<int>(structure.atoms.size());
		structure.atoms.push_back(std::move(atom));
		return {};
	}

	Result<void> ApplyReplacement(
		CrystalStructure &structure,
		const PointDefectOperation &operation,
		const char *operationName)
	{
		if (operation.atomIndex >= structure.atoms.size())
			return MakeInvalidDefectOperationError(operation.atomIndex, structure.atoms.size(), operationName);
		if (operation.replacementSpecies.empty())
		{
			return StructuredError{
				ErrorCategory::Validation,
				Severity::Error,
				"Defect operation failed.",
				std::string(operationName) + " has an empty replacement species.",
				"Provide a chemical symbol for replacementSpecies.",
				"DefectModel",
				"domain.defect.empty_replacement_species"};
		}

		AtomSite &atom = structure.atoms[operation.atomIndex];
		atom.species = operation.replacementSpecies;
		if (!operation.label.empty())
			atom.label = operation.label;
		return {};
	}

	Result<CrystalStructure> BuildDefectedStructure(
		const CrystalStructure &pristine,
		const DefectConfiguration &configuration)
	{
		CrystalStructure defected = pristine;
		for (const PointDefectOperation &operation : configuration.operations)
		{
			Result<void> result;
			switch (operation.type)
			{
				case PointDefectType::None:
					return StructuredError{
						ErrorCategory::Validation,
						Severity::Error,
						"Defect operation failed.",
						"Defect operation has no selected point defect type.",
						"Select a valid point defect type before building the defected structure.",
						"DefectModel",
						"domain.defect.missing_type"};
				case PointDefectType::Vacancy:
					result = ApplyVacancy(defected, operation);
					break;
				case PointDefectType::Interstitial:
					result = ApplyInterstitial(defected, operation);
					break;
				case PointDefectType::Antisite:
					result = ApplyReplacement(defected, operation, "Antisite");
					break;
				case PointDefectType::SubstitutionalDopant:
					result = ApplyReplacement(defected, operation, "Substitutional dopant");
					break;
			}

			if (!result)
				return result.Error();
		}

		return defected;
	}
} // namespace DefectStudio
