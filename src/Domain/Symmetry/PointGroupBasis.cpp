#include "Core/dspch.hpp"

#include "Domain/Symmetry/PointGroupBasis.hpp"

#include <cmath>
#include <cstdint>
#include <limits>
#include <string>

namespace DefectStudio
{
	namespace
	{
		[[nodiscard]] StructuredError BasisError(const char *code, const std::string &message)
		{
			return StructuredError{
				ErrorCategory::Validation,
				Severity::Error,
				message,
				message,
				"Correct the selection or the structure geometry.",
				"Domain/Symmetry/PointGroupBasis",
				code};
		}

		void HashBytes(std::uint64_t &hash, const void *data, std::size_t size)
		{
			const auto *bytes = static_cast<const std::uint8_t *>(data);
			for (std::size_t index = 0; index < size; ++index)
			{
				hash ^= bytes[index];
				hash *= 1099511628211ull;
			}
		}

		void HashString(std::uint64_t &hash, const std::string &value)
		{
			HashBytes(hash, value.data(), value.size());
		}

		[[nodiscard]] std::int64_t Quantize(double value)
		{
			return static_cast<std::int64_t>(std::llround(value * 1'000'000.0));
		}
	} // namespace

	Result<SelectionBasis> BuildSelectionBasis(
		const CrystalStructure &structure, const std::vector<std::size_t> &atomIndices, const glm::dvec3 &centre)
	{
		if (atomIndices.empty())
			return BasisError("symmetry.basis.empty_selection", "The selected atom list is empty.");
		for (const std::size_t atomIndex : atomIndices)
			if (atomIndex >= structure.atoms.size())
				return BasisError("symmetry.basis.index_out_of_range", "A selected atom index is out of range.");

		const bool periodic = structure.isPeriodic;
		glm::dmat3 lattice(1.0);
		if (periodic)
		{
			lattice = glm::dmat3(structure.cell.ToMatrix());
			if (std::abs(glm::determinant(lattice)) < 1e-8)
				return BasisError("symmetry.basis.singular_lattice", "The periodic lattice is not invertible.");
		}
		const glm::dmat3 inverse = periodic ? glm::inverse(lattice) : glm::dmat3(1.0);

		SelectionBasis result;
		result.atomIndices = atomIndices;
		result.centre = centre;
		result.periodicUnwrapped = periodic;
		result.sites.reserve(atomIndices.size());
		std::uint64_t hash = 1469598103934665603ull;
		for (const std::size_t atomIndex : atomIndices)
		{
			const AtomSite &atom = structure.atoms[atomIndex];
			glm::dvec3 position = glm::dvec3(atom.position) - centre;
			if (periodic)
			{
				glm::dvec3 fractionalDelta = inverse * position;
				for (int component = 0; component < 3; ++component)
					fractionalDelta[component] -= std::floor(fractionalDelta[component] + 0.5);
				position = lattice * fractionalDelta;
			}

			const std::string label = atom.species + std::to_string(atomIndex);
			result.sites.push_back(BasisSite{label, position, atom.species});
			HashBytes(hash, &atomIndex, sizeof(atomIndex));
			HashString(hash, atom.species);
			for (int component = 0; component < 3; ++component)
			{
				const std::int64_t quantized = Quantize(position[component]);
				HashBytes(hash, &quantized, sizeof(quantized));
			}
		}
		result.hash = hash;
		return result;
	}
} // namespace DefectStudio
