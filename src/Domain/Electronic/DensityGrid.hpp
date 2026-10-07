#pragma once

#include <array>
#include <optional>
#include <string_view>

#include "Domain/Electronic/ElectronicStructureModel.hpp"

namespace DefectStudio
{
	// Which block of a CHGCAR a density object shows. Magnetization is rho_up - rho_down (the spin
	// density, signed); SpinUp/SpinDown are (total +- magnetization) / 2. Only Magnetization and a
	// difference against a reference CHGCAR can go negative.
	enum class DensityComponent
	{
		Total,
		Magnetization,
		SpinUp,
		SpinDown,
	};

	inline constexpr std::array<DensityComponent, 4> kDensityComponents = {
		DensityComponent::Total, DensityComponent::Magnetization, DensityComponent::SpinUp,
		DensityComponent::SpinDown};

	// The argument vasp_density_grid_load.py takes, also the YAML spelling.
	[[nodiscard]] constexpr const char *DensityComponentKey(const DensityComponent component)
	{
		switch (component)
		{
		case DensityComponent::Total: return "total";
		case DensityComponent::Magnetization: return "magnetization";
		case DensityComponent::SpinUp: return "up";
		case DensityComponent::SpinDown: return "down";
		}
		return "total";
	}

	[[nodiscard]] constexpr const char *DensityComponentDisplayName(const DensityComponent component)
	{
		switch (component)
		{
		case DensityComponent::Total: return "Gęstość całkowita";
		case DensityComponent::Magnetization: return "Gęstość spinowa (↑−↓)";
		case DensityComponent::SpinUp: return "Spin ↑";
		case DensityComponent::SpinDown: return "Spin ↓";
		}
		return "";
	}

	[[nodiscard]] constexpr std::optional<DensityComponent> ParseDensityComponent(const std::string_view key)
	{
		for (const DensityComponent component : kDensityComponents)
			if (key == DensityComponentKey(component))
				return component;
		return std::nullopt;
	}

	// Whole-cell numbers for the loaded grid (after any reference subtraction), e/Ang^3 for the
	// extrema and electrons for the integrals. integral of the magnetization is the cell's moment in
	// mu_B; absIntegral > |integral| means opposite-spin regions are present.
	struct DensityGridStatistics
	{
		float integral = 0.0f;
		float absIntegral = 0.0f;
		float minimum = 0.0f;
		float maximum = 0.0f;
		int atomCount = 0;
	};

	// One density component on the CHGCAR's own grid. grid.values are e/Ang^3, C-order (x slowest),
	// grid.origin zero (the box is the cell); energy/occupation unused.
	struct DensityGrid
	{
		OrbitalGridData grid;
		DensityGridStatistics statistics;
	};
} // namespace DefectStudio
