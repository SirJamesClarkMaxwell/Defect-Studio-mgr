#include "Core/dspch.hpp"

#include "Domain/Crystal/BravaisLattice.hpp"

namespace DefectStudio
{
	LatticeFieldConstraints GetFieldConstraints(CrystalSystem system)
	{
		LatticeFieldConstraints c;
		switch (system)
		{
			case CrystalSystem::Cubic:
				c.bLocked = c.cLocked = c.alphaLocked = c.betaLocked = c.gammaLocked = true;
				c.lockedAngleDegrees = 90.0f;
				break;
			case CrystalSystem::Tetragonal:
				c.bLocked = true; // b = a, c free
				c.alphaLocked = c.betaLocked = c.gammaLocked = true;
				c.lockedAngleDegrees = 90.0f;
				break;
			case CrystalSystem::Orthorhombic:
				c.alphaLocked = c.betaLocked = c.gammaLocked = true; // a, b, c all free
				c.lockedAngleDegrees = 90.0f;
				break;
			case CrystalSystem::Hexagonal:
				c.bLocked = true; // b = a, c free
				c.alphaLocked = c.betaLocked = true; // gamma stays 120, not user-editable
				c.gammaLocked = true;
				c.lockedAngleDegrees = 90.0f; // alpha/beta; gamma is handled specially in BuildLatticeCell
				break;
			case CrystalSystem::Trigonal:
				c.bLocked = c.cLocked = true; // a = b = c
				c.betaLocked = c.gammaLocked = true; // alpha = beta = gamma, alpha itself stays free
				break;
			case CrystalSystem::Monoclinic:
				c.alphaLocked = c.gammaLocked = true; // beta free
				c.lockedAngleDegrees = 90.0f;
				break;
			case CrystalSystem::Triclinic:
				break; // nothing locked
		}
		return c;
	}

	LatticeCell BuildLatticeCell(CrystalSystem system, const LatticeParameters &params)
	{
		float a = params.a;
		float b = params.b;
		float c = params.c;
		float alpha = glm::radians(params.alphaDegrees);
		float beta = glm::radians(params.betaDegrees);
		float gamma = glm::radians(params.gammaDegrees);

		switch (system)
		{
			case CrystalSystem::Cubic:
				b = c = a;
				alpha = beta = gamma = glm::radians(90.0f);
				break;
			case CrystalSystem::Tetragonal:
				b = a;
				alpha = beta = gamma = glm::radians(90.0f);
				break;
			case CrystalSystem::Orthorhombic:
				alpha = beta = gamma = glm::radians(90.0f);
				break;
			case CrystalSystem::Hexagonal:
				b = a;
				alpha = beta = glm::radians(90.0f);
				gamma = glm::radians(120.0f);
				break;
			case CrystalSystem::Trigonal:
				b = c = a;
				beta = gamma = alpha;
				break;
			case CrystalSystem::Monoclinic:
				alpha = gamma = glm::radians(90.0f);
				break;
			case CrystalSystem::Triclinic:
				break;
		}

		// Standard crystallographic convention (matches pymatgen Lattice.from_parameters / ASE
		// cellpar_to_cell): a along +X; b in the XY plane at angle gamma from a; c completes the
		// set so that the angle to a is beta and to b is alpha.
		LatticeCell cell;
		cell.vectors[0] = glm::vec3(a, 0.0f, 0.0f);
		cell.vectors[1] = glm::vec3(b * glm::cos(gamma), b * glm::sin(gamma), 0.0f);

		const float cx = c * glm::cos(beta);
		const float cy = c * (glm::cos(alpha) - glm::cos(beta) * glm::cos(gamma)) / glm::sin(gamma);
		const float czSquared = c * c - cx * cx - cy * cy;
		const float cz = czSquared > 0.0f ? glm::sqrt(czSquared) : 0.0f;
		cell.vectors[2] = glm::vec3(cx, cy, cz);

		return cell;
	}

	std::vector<glm::vec3> GetCenteringPresetBasis(BravaisCenteringPreset preset)
	{
		switch (preset)
		{
			case BravaisCenteringPreset::Primitive:
				return { glm::vec3(0.0f, 0.0f, 0.0f) };
			case BravaisCenteringPreset::BodyCentered:
				return { glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.5f, 0.5f, 0.5f) };
			case BravaisCenteringPreset::FaceCentered:
				return {
					glm::vec3(0.0f, 0.0f, 0.0f),
					glm::vec3(0.5f, 0.5f, 0.0f),
					glm::vec3(0.5f, 0.0f, 0.5f),
					glm::vec3(0.0f, 0.5f, 0.5f)};
			case BravaisCenteringPreset::BaseCentered:
				return { glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.5f, 0.5f, 0.0f) };
		}
		return { glm::vec3(0.0f, 0.0f, 0.0f) };
	}
} // namespace DefectStudio
