#include "Core/dspch.hpp"

#include "Domain/Crystal/StructureValidation.hpp"

#include <cmath>

namespace DefectStudio
{
	namespace
	{
		constexpr float MinimumCellVolume = 1e-6f; // Angstrom^3; below this the cell is degenerate

		[[nodiscard]] bool IsFinite(const glm::vec3 &vector) noexcept
		{
			return std::isfinite(vector.x) && std::isfinite(vector.y) && std::isfinite(vector.z);
		}

		[[nodiscard]] StructuredError ValidationError(std::string userMessage, std::string technicalDetails)
		{
			return StructuredError(
				ErrorCategory::Validation,
				Severity::Error,
				std::move(userMessage),
				std::move(technicalDetails),
				"Fix the structure in the editor before adding it to the project.",
				"ValidateStructureForPersistence");
		}
	} // namespace

	Result<void> ValidateStructureForPersistence(const CrystalStructure &structure)
	{
		if (structure.atoms.empty())
			return ValidationError("Structure has no atoms", "atoms vector is empty");

		for (const glm::vec3 &vector : structure.cell.vectors)
		{
			if (!IsFinite(vector))
				return ValidationError("Structure has an invalid unit cell", "Cell vector contains NaN or infinity");
		}

		const float volume = std::abs(glm::determinant(structure.cell.ToMatrix()));
		if (!std::isfinite(volume) || volume < MinimumCellVolume)
			return ValidationError(
				"Structure has a degenerate unit cell",
				"Cell volume " + std::to_string(volume) + " is below the minimum of " + std::to_string(MinimumCellVolume));

		for (std::size_t i = 0; i < structure.atoms.size(); ++i)
		{
			const AtomSite &atom = structure.atoms[i];
			if (atom.species.empty())
				return ValidationError("Structure has an atom without an element", "Atom " + std::to_string(i) + " has an empty species");

			if (!IsFinite(atom.fractional) || !IsFinite(atom.position))
				return ValidationError(
					"Structure has an atom at an invalid position",
					"Atom " + std::to_string(i) + " has NaN or infinite coordinates");
		}

		return Result<void>{};
	}
} // namespace DefectStudio
