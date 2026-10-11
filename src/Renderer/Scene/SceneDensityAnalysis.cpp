#include "Core/dspch.hpp"

#include "Renderer/Scene/SceneDensityAnalysis.hpp"

#include <cmath>
#include <format>

namespace DefectStudio
{
	std::optional<glm::vec3> SceneDensityAnalysisCentre(const RendererWindowState::SceneDensity &density)
	{
		if (density.analysisCentre)
			return density.analysisCentre;
		if (density.data && density.data->centroid)
			return density.data->centroid->cartesian;
		return std::nullopt;
	}

	std::optional<glm::vec3> SceneDensityAnalysisCentreFractional(const RendererWindowState::SceneDensity &density)
	{
		const auto centre = SceneDensityAnalysisCentre(density);
		if (!density.data || !centre)
			return std::nullopt;
		const OrbitalGridData &grid = density.data->grid;
		const float determinant = glm::determinant(grid.cell);
		if (!std::isfinite(determinant) || std::abs(determinant) <= 1e-6f)
			return std::nullopt;
		const glm::vec3 fractional = glm::inverse(grid.cell) * (*centre - grid.origin);
		for (int axis = 0; axis < 3; ++axis)
			if (!std::isfinite(fractional[axis]))
				return std::nullopt;
		return fractional - glm::floor(fractional);
	}

	const DensityRadialProfile *SceneDensityRadialProfile(RendererWindowState::SceneDensity &density)
	{
		const auto centre = SceneDensityAnalysisCentre(density);
		if (!density.data || !centre)
		{
			density.radialProfile.reset();
			density.radialProfileGrid = nullptr;
			return nullptr;
		}
		if (!density.radialProfile || density.radialProfileGrid != density.data.get() ||
			density.radialProfileCentre != *centre)
		{
			density.radialProfile = CreateRef<const DensityRadialProfile>(AbsoluteDensityRadialProfile(density.data->grid, *centre));
			density.radialProfileGrid = density.data.get();
			density.radialProfileCentre = *centre;
		}
		return density.radialProfile.get();
	}

	std::optional<glm::vec3> SelectedAtomsCentroid(const RendererWindowState &windowState)
	{
		glm::dvec3 sum(0.0);
		std::size_t count = 0;
		for (const std::size_t index : windowState.selectedAtomIndices)
			if (index < windowState.structure.atoms.size())
			{
				sum += glm::dvec3(windowState.structure.atoms[index].cartesianPosition);
				++count;
			}
		if (count == 0)
			return std::nullopt;
		return glm::vec3(sum / static_cast<double>(count));
	}

	std::string FormatSxdefectalignCentre(const glm::vec3 &fractional)
	{
		const glm::vec3 wrapped = fractional - glm::floor(fractional);
		return std::format("{:.6f},{:.6f},{:.6f}", wrapped.x, wrapped.y, wrapped.z);
	}
} // namespace DefectStudio
