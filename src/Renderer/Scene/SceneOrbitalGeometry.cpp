#include "Core/dspch.hpp"
#include "Renderer/Scene/SceneOrbitalGeometry.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <optional>
#include <limits>
#include <cstdint>
#include <iterator>
#include <string_view>

#include <glm/gtc/quaternion.hpp>

namespace DefectStudio
{
	namespace
	{
		void HashValue(std::uint64_t &hash, std::uint64_t value)
		{
			hash ^= value;
			hash *= 1099511628211ull;
		}

		void HashFloat(std::uint64_t &hash, float value)
		{
			HashValue(hash, std::bit_cast<std::uint32_t>(value));
		}

		void HashVec3(std::uint64_t &hash, const glm::vec3 &value)
		{
			HashFloat(hash, value.x);
			HashFloat(hash, value.y);
			HashFloat(hash, value.z);
		}

		[[nodiscard]] glm::vec3 SanitizedStretch(const glm::vec3 &stretch)
		{
			glm::vec3 result = stretch;
			for (int axis = 0; axis < 3; ++axis)
				if (!std::isfinite(result[axis]) || result[axis] <= 0.0f)
					result[axis] = 1.0f;
			return result;
		}

		// Must match RotatedEulerDegrees (SceneTransform.cpp) exactly, or the gizmo's rotate delta
		// and this render-time frame disagree on what a given rotationEuler triple means. That
		// function composes and decomposes through glm::quat(vec3)/glm::eulerAngles - GLM's own
		// fixed convention - so this has to go through the same pair rather than a hand-rolled
		// Rx*Ry*Rz composition (which is a *different* Euler order than glm::quat's, and was the
        // bug: dragging a rotate handle updates rotationEuler in GLM's convention, but this used
		// to reinterpret those same numbers as X-then-Y-then-Z, so a Z-axis drag did not read back
		// as a Z-axis rotation of the mesh - it read as some other axis' worth, which could look
		// like "no visible rotation" depending on the starting orientation).
		[[nodiscard]] glm::mat3 RotationFrame(const glm::vec3 &rotationEuler)
		{
			return glm::mat3_cast(glm::quat(glm::radians(rotationEuler)));
		}

		[[nodiscard]] glm::mat3 OrbitalFrame(
			const RendererWindowState::SceneOrbital &orbital, const SceneOrbitalCenters &centers)
		{
			if (!orbital.lcaoComponents.empty())
				return glm::mat3(1.0f);
			if (!IsTwoCenterPreset(orbital.preset))
				return RotationFrame(orbital.rotationEuler);

			glm::vec3 bond = centers.centerB - centers.centerA;
			if (glm::dot(bond, bond) <= 1e-12f)
				bond = glm::vec3(1.0f, 0.0f, 0.0f);

			const glm::vec3 zAxis = glm::normalize(bond);
			// A fixed reference makes this frame deterministic. Fall back only when it is nearly
			// parallel to the bond, where their cross product would be numerically unstable. These
			// choices mirror OrbitalPresets' two-centre frame, including its coincident-centre fallback.
			const glm::vec3 reference = std::abs(zAxis.z) > 0.999f
				? glm::vec3(0.0f, 1.0f, 0.0f)
				: glm::vec3(0.0f, 0.0f, 1.0f);
			const glm::vec3 xAxis = glm::normalize(glm::cross(reference, zAxis));
			const glm::vec3 yAxis = glm::cross(zAxis, xAxis);
			return glm::mat3(xAxis, yAxis, zAxis);
		}

		[[nodiscard]] int AtomicNumber(std::string_view element)
		{
			constexpr std::array<std::string_view, 118> symbols = {
				"H", "He", "Li", "Be", "B", "C", "N", "O", "F", "Ne", "Na", "Mg", "Al", "Si", "P", "S", "Cl", "Ar",
				"K", "Ca", "Sc", "Ti", "V", "Cr", "Mn", "Fe", "Co", "Ni", "Cu", "Zn", "Ga", "Ge", "As", "Se", "Br", "Kr",
				"Rb", "Sr", "Y", "Zr", "Nb", "Mo", "Tc", "Ru", "Rh", "Pd", "Ag", "Cd", "In", "Sn", "Sb", "Te", "I", "Xe",
				"Cs", "Ba", "La", "Ce", "Pr", "Nd", "Pm", "Sm", "Eu", "Gd", "Tb", "Dy", "Ho", "Er", "Tm", "Yb", "Lu",
				"Hf", "Ta", "W", "Re", "Os", "Ir", "Pt", "Au", "Hg", "Tl", "Pb", "Bi", "Po", "At", "Rn", "Fr", "Ra",
				"Ac", "Th", "Pa", "U", "Np", "Pu", "Am", "Cm", "Bk", "Cf", "Es", "Fm", "Md", "No", "Lr", "Rf", "Db",
				"Sg", "Bh", "Hs", "Mt", "Ds", "Rg", "Cn", "Nh", "Fl", "Mc", "Lv", "Ts", "Og"};
			const auto found = std::find(symbols.begin(), symbols.end(), element);
			return found == symbols.end() ? 0 : static_cast<int>(std::distance(symbols.begin(), found)) + 1;
		}

		[[nodiscard]] std::array<int, 8> ShellPopulations(int atomicNumber)
		{
			struct Subshell
			{
				int shell;
				int capacity;
			};
			constexpr std::array<Subshell, 19> fillingOrder = {{
				{1, 2}, {2, 2}, {2, 6}, {3, 2}, {3, 6}, {4, 2}, {3, 10}, {4, 6}, {5, 2}, {4, 10},
				{5, 6}, {6, 2}, {4, 14}, {5, 10}, {6, 6}, {7, 2}, {5, 14}, {6, 10}, {7, 6}}};
			std::array<int, 8> populations{};
			int remaining = atomicNumber;
			for (const Subshell subshell : fillingOrder)
			{
				const int occupied = std::min(remaining, subshell.capacity);
				populations[static_cast<std::size_t>(subshell.shell)] += occupied;
				remaining -= occupied;
				if (remaining == 0)
					break;
			}
			return populations;
		}
	} // namespace

	glm::vec3 ResolveAnchor(
		const glm::vec3 &fallback,
		const std::optional<std::size_t> &anchor,
		const RendererStructureData &structure)
	{
		if (!anchor.has_value() || *anchor >= structure.atoms.size())
			return fallback;
		return structure.atoms[*anchor].cartesianPosition;
	}

	glm::vec3 ResolveAnchor(
		const glm::vec3 &fallback,
		const std::vector<std::size_t> &anchors,
		const std::size_t anchorIndex,
		const RendererStructureData &structure)
	{
		return ResolveAnchor(
			fallback,
			anchorIndex < anchors.size() ? std::optional<std::size_t>(anchors[anchorIndex]) : std::nullopt,
			structure);
	}

	[[nodiscard]] bool IsTwoCenterPreset(OrbitalPreset preset)
	{
		switch (preset)
		{
			case OrbitalPreset::Sigma:
			case OrbitalPreset::SigmaStar:
			case OrbitalPreset::Pi:
			case OrbitalPreset::PiStar:
			case OrbitalPreset::Delta:
			case OrbitalPreset::DeltaStar:
			case OrbitalPreset::SpSigma:
			case OrbitalPreset::SpSigmaStar:
			case OrbitalPreset::Sp2Sigma:
			case OrbitalPreset::Sp2SigmaStar:
			case OrbitalPreset::Sp3Sigma:
			case OrbitalPreset::Sp3SigmaStar: return true;
			default: return false;
		}
	}

	SceneOrbitalCenters ResolveSceneOrbitalCenters(
		const RendererWindowState::SceneOrbital &orbital, const RendererStructureData &structure)
	{
		SceneOrbitalCenters centers;
		if (!orbital.lcaoComponents.empty())
		{
			for (const auto &component : orbital.lcaoComponents)
				centers.centroid += ResolveAnchor(component.center, component.anchorAtom, structure);
			centers.centroid /= static_cast<float>(orbital.lcaoComponents.size());
			centers.centerA = centers.centerB = centers.centroid;
			return centers;
		}
		centers.centerA = ResolveAnchor(orbital.centerA, orbital.anchorAtoms, 0, structure);
		centers.centerB = ResolveAnchor(orbital.centerB, orbital.anchorAtoms, 1, structure);
		centers.centroid = IsTwoCenterPreset(orbital.preset)
			? (centers.centerA + centers.centerB) * 0.5f
			: centers.centerA;
		return centers;
	}

	OrbitalWavefunction BuildOrbitalWavefunction(
		const RendererWindowState::SceneOrbital &orbital, const RendererStructureData &structure)
	{
		if (!orbital.lcaoComponents.empty())
		{
			OrbitalWavefunction combined;
			for (const auto &component : orbital.lcaoComponents)
			{
				if (IsTwoCenterPreset(component.preset))
					continue;
				OrbitalPresetSettings settings;
				settings.centerA = ResolveAnchor(component.center, component.anchorAtom, structure);
				settings.shell = component.shell;
				settings.lobeIndex = component.lobeIndex;
				settings.effectiveCharge = component.effectiveCharge;
				settings.orientation = RotationFrame(component.rotationEuler);
				for (OrbitalTerm term : MakeOrbitalPreset(component.preset, settings).terms)
				{
					term.coefficient *= component.coefficient;
					if (orbital.phaseFlipped)
						term.coefficient = -term.coefficient;
					combined.terms.push_back(term);
				}
			}
			return combined;
		}
		const SceneOrbitalCenters centers = ResolveSceneOrbitalCenters(orbital, structure);
		OrbitalPresetSettings settings;
		settings.centerA = centers.centerA;
		settings.centerB = centers.centerB;
		settings.effectiveCharge = orbital.effectiveCharge;
		settings.lobeIndex = orbital.lobeIndex;
		settings.shell = orbital.shell;
		if (!IsTwoCenterPreset(orbital.preset))
			settings.orientation = RotationFrame(orbital.rotationEuler);
		OrbitalWavefunction wavefunction = MakeOrbitalPreset(orbital.preset, settings);
		if (orbital.phaseFlipped)
		{
			for (OrbitalTerm &term : wavefunction.terms)
				term.coefficient = -term.coefficient;
		}
		return wavefunction;
	}

	std::vector<IsosurfaceVertex> BuildSceneOrbitalMesh(
		const RendererWindowState::SceneOrbital &orbital, const RendererStructureData &structure)
	{
		if (orbital.resolution < 2 || !std::isfinite(orbital.isoFraction) || orbital.isoFraction <= 0.0f ||
			!std::isfinite(orbital.scale) || orbital.scale <= 0.0f)
			return {};

		OrbitalSamplingSettings sampling;
		sampling.dimensions = glm::ivec3(orbital.resolution);
		const OrbitalGridData grid = SampleOrbitalToGrid(BuildOrbitalWavefunction(orbital, structure), sampling);
		const float isoValue = SuggestOrbitalIsoValue(grid, orbital.isoFraction);
		if (!std::isfinite(isoValue) || isoValue <= 0.0f)
			return {};

		std::vector<IsosurfaceVertex> mesh = GenerateIsosurfaceMesh(grid, isoValue);
		const SceneOrbitalCenters centers = ResolveSceneOrbitalCenters(orbital, structure);
		const glm::vec3 centroid = centers.centroid;
		const glm::vec3 stretch = SanitizedStretch(orbital.stretch);
		const glm::vec3 inverseStretch = glm::vec3(1.0f) / stretch;
		const glm::mat3 frame = OrbitalFrame(orbital, centers);
		const glm::mat3 inverseFrame = glm::transpose(frame);
		// GenerateIsosurfaceMesh uses periodic-grid i/N coordinates while the analytic sampler uses
		// endpoint-inclusive i/(N-1). Keep the documented (N-1)/N size difference, but translate the
		// shrunken box back onto its physical centroid instead of leaving it half a voxel off-centre.
		const glm::vec3 centeringOffset =
			grid.cell[0] * (0.5f / static_cast<float>(grid.dimensions.x)) +
			grid.cell[1] * (0.5f / static_cast<float>(grid.dimensions.y)) +
			grid.cell[2] * (0.5f / static_cast<float>(grid.dimensions.z));
		for (IsosurfaceVertex &vertex : mesh)
		{
			const glm::vec3 offset = vertex.position + centeringOffset - centroid;
			vertex.position = centroid + frame * (inverseFrame * offset * stretch) * orbital.scale;
			vertex.normal = glm::normalize(frame * (inverseFrame * vertex.normal * inverseStretch));
		}
		return mesh;
	}

	SceneOrbitalMeshKey MakeSceneOrbitalMeshKey(
		const RendererWindowState::SceneOrbital &orbital, const RendererStructureData &structure)
	{
		const SceneOrbitalCenters centers = ResolveSceneOrbitalCenters(orbital, structure);
		std::uint64_t hash = 1469598103934665603ull;
		HashValue(hash, orbital.lcaoComponents.size());
		if (!orbital.lcaoComponents.empty())
		{
			for (const auto &component : orbital.lcaoComponents)
			{
				HashValue(hash, component.anchorAtom);
				HashValue(hash, static_cast<std::uint64_t>(component.preset));
				HashValue(hash, static_cast<std::uint64_t>(component.shell));
				HashValue(hash, static_cast<std::uint64_t>(component.lobeIndex));
				HashFloat(hash, component.effectiveCharge);
				HashFloat(hash, component.coefficient);
				HashVec3(hash, ResolveAnchor(component.center, component.anchorAtom, structure));
				HashVec3(hash, component.rotationEuler);
			}
		}
		else
		{
			HashValue(hash, static_cast<std::uint64_t>(orbital.preset));
			HashValue(hash, static_cast<std::uint64_t>(orbital.shell));
			HashValue(hash, static_cast<std::uint64_t>(orbital.lobeIndex));
			HashFloat(hash, orbital.effectiveCharge);
			HashVec3(hash, centers.centerA);
			HashVec3(hash, centers.centerB);
			HashVec3(hash, orbital.rotationEuler);
		}
		HashValue(hash, static_cast<std::uint64_t>(orbital.phaseFlipped));
		HashFloat(hash, orbital.scale);
		HashVec3(hash, orbital.stretch);
		HashFloat(hash, orbital.isoFraction);
		HashValue(hash, static_cast<std::uint64_t>(orbital.resolution));
		return SceneOrbitalMeshKey{hash};
	}

	void ResolveAnchoredOrbitals(RendererWindowState &windowState)
	{
		for (RendererWindowState::SceneOrbital &orbital : windowState.sceneOrbitals)
		{
			for (auto &component : orbital.lcaoComponents)
				component.center = ResolveAnchor(component.center, component.anchorAtom, windowState.structure);
			if (!orbital.lcaoComponents.empty())
				continue;
			if (!orbital.anchorAtoms.empty() && orbital.anchorAtoms[0] < windowState.structure.atoms.size())
				orbital.centerA = windowState.structure.atoms[orbital.anchorAtoms[0]].cartesianPosition;
			if (orbital.anchorAtoms.size() >= 2 && orbital.anchorAtoms[1] < windowState.structure.atoms.size())
				orbital.centerB = windowState.structure.atoms[orbital.anchorAtoms[1]].cartesianPosition;
		}
	}

	RendererWindowState::SceneOrbital MakeDefaultSceneOrbital(
		const RendererWindowState &windowState, OrbitalPreset preset, const glm::vec3 &seedPosition)
	{
		return MakeDefaultSceneOrbital(windowState, preset, seedPosition, windowState.selectedAtomIndices);
	}

	RendererWindowState::SceneOrbital MakeDefaultSceneOrbital(
		const RendererWindowState &windowState,
		OrbitalPreset preset,
		const glm::vec3 &seedPosition,
		const std::vector<std::size_t> &anchorAtoms)
	{
		RendererWindowState::SceneOrbital orbital;
		orbital.preset = preset;
		orbital.centerA = seedPosition;
		orbital.centerB = seedPosition + glm::vec3(1.5f, 0.0f, 0.0f);

		const std::size_t requiredAtoms = IsTwoCenterPreset(preset) ? 2u : 1u;
		if (anchorAtoms.size() != requiredAtoms ||
			!std::all_of(anchorAtoms.begin(), anchorAtoms.end(),
				[&](std::size_t index) { return index < windowState.structure.atoms.size(); }))
			return orbital;

		orbital.anchorAtoms = anchorAtoms;
		orbital.centerA = windowState.structure.atoms[orbital.anchorAtoms[0]].cartesianPosition;
		orbital.centerB = requiredAtoms == 2
			? windowState.structure.atoms[orbital.anchorAtoms[1]].cartesianPosition
			: orbital.centerB;

		float chargeSum = 0.0f;
		int shell = 1;
		for (const std::size_t atomIndex : orbital.anchorAtoms)
		{
			const std::string &element = windowState.structure.atoms[atomIndex].element;
			chargeSum += ValenceEffectiveCharge(element);
			shell = std::max(shell, ValenceShell(element));
		}
		orbital.effectiveCharge = chargeSum / static_cast<float>(orbital.anchorAtoms.size());
		orbital.shell = shell;
		return orbital;
	}

	SceneOrbitalBounds SceneOrbitalWorldBounds(
		const RendererWindowState::SceneOrbital &orbital, const RendererStructureData &structure)
	{
		SceneOrbitalBounds bounds;
		bounds.center = ResolveSceneOrbitalCenters(orbital, structure).centroid;
		// SuggestOrbitalExtent is a measured radius: the outermost distance from the centroid at
		// which the wavefunction still carries 2% of its peak amplitude. The drawn isosurface sits
		// at 20% by default and so lies strictly inside it - which is exactly what makes this a
		// sound bounding sphere rather than a guess that happens to work for the shapes tried.
		const OrbitalWavefunction wavefunction = BuildOrbitalWavefunction(orbital, structure);
		float extent = SuggestOrbitalExtent(wavefunction);
		// Mixed component presets can have different term counts, so their term centroid differs
		// from the scene pivot. Expand the same measured sphere to cover that offset.
		if (!orbital.lcaoComponents.empty() && extent > 0.0f)
			extent += glm::length(OrbitalCentroid(wavefunction) - bounds.center);
		const float scale = std::isfinite(orbital.scale) && orbital.scale > 0.0f ? orbital.scale : 1.0f;
		const glm::vec3 stretch = SanitizedStretch(orbital.stretch);
		const float largestStretch = std::max({stretch.x, stretch.y, stretch.z});
		bounds.radius = std::isfinite(extent) && extent > 0.0f ? extent * scale * largestStretch : 0.0f;
		return bounds;
	}

	std::optional<std::size_t> PickSceneOrbital(
		const RendererWindowState &windowState,
		const RendererStructureData &structure,
		const glm::vec3 &rayOrigin,
		const glm::vec3 &rayDirection)
	{
		const float directionLength = glm::length(rayDirection);
		if (!std::isfinite(directionLength) || directionLength <= 0.0f)
			return std::nullopt;
		const glm::vec3 direction = rayDirection / directionLength;

		std::optional<std::size_t> nearest;
		float nearestDistance = std::numeric_limits<float>::max();
		for (std::size_t index = 0; index < windowState.sceneOrbitals.size(); ++index)
		{
			const RendererWindowState::SceneOrbital &orbital = windowState.sceneOrbitals[index];
			if (!orbital.visible)
				continue;

			const SceneOrbitalBounds bounds = SceneOrbitalWorldBounds(orbital, structure);
			if (bounds.radius <= 0.0f)
				continue;

			const glm::vec3 toCenter = bounds.center - rayOrigin;
			const float alongRay = glm::dot(toCenter, direction);
			const float perpendicularSquared = glm::dot(toCenter, toCenter) - alongRay * alongRay;
			const float radiusSquared = bounds.radius * bounds.radius;
			if (perpendicularSquared > radiusSquared)
				continue;

			// Distance to where the ray enters the sphere. Standing inside one counts as a hit at
			// zero rather than as a miss behind the camera, so an orbital you have flown into is
			// still clickable.
			const float halfChord = std::sqrt(radiusSquared - perpendicularSquared);
			const float entry = alongRay - halfChord;
			const float exit = alongRay + halfChord;
			if (exit < 0.0f)
				continue;
			const float distance = std::max(0.0f, entry);
			if (distance < nearestDistance)
			{
				nearestDistance = distance;
				nearest = index;
			}
		}
		return nearest;
	}

	int ValenceShell(const std::string &element)
	{
		const int atomicNumber = AtomicNumber(element);
		if (atomicNumber == 0)
			return 1;
		const std::array<int, 8> populations = ShellPopulations(atomicNumber);
		for (int shell = 7; shell >= 1; --shell)
			if (populations[static_cast<std::size_t>(shell)] > 0)
				return shell;
		return 1;
	}

	float ValenceEffectiveCharge(const std::string &element)
	{
		const int atomicNumber = AtomicNumber(element);
		if (atomicNumber == 0)
			return 1.0f;
		const std::array<int, 8> populations = ShellPopulations(atomicNumber);
		const int shell = ValenceShell(element);
		const float sameShellShielding = (shell == 1 ? 0.30f : 0.35f) *
			static_cast<float>(std::max(0, populations[static_cast<std::size_t>(shell)] - 1));
		const float previousShellShielding = shell > 1
			? 0.85f * static_cast<float>(populations[static_cast<std::size_t>(shell - 1)])
			: 0.0f;
		int innerElectrons = 0;
		for (int innerShell = 1; innerShell + 1 < shell; ++innerShell)
			innerElectrons += populations[static_cast<std::size_t>(innerShell)];
		return std::max(0.1f, static_cast<float>(atomicNumber) - sameShellShielding -
			previousShellShielding - static_cast<float>(innerElectrons));
	}
} // namespace DefectStudio
