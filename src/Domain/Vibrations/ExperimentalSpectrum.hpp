#pragma once

#include <span>
#include <string>
#include <vector>

namespace DefectStudio
{
	// task/113f: a measured PL spectrum on the photon-energy axis the computed spectra use.
	// Extension point: add a unit here and its conversion in ToElectronVolt (and the Jacobian, for units that
	// are not linear in energy, in BuildExperimentalSpectrum) - nothing else needs to change.
	enum class SpectrumAxisUnit
	{
		ElectronVolt,
	};

	struct ExperimentalSpectrum
	{
		std::vector<double> energyEv;  // strictly ascending
		std::vector<double> intensity; // as measured (no normalisation; the plots normalise)
	};

	[[nodiscard]] double ToElectronVolt(double value, SpectrumAxisUnit unit);

	// Pairs (x, y) with both finite are converted to eV, sorted ascending, and points with an equal energy are
	// averaged. False (outError set, out untouched) when x and y differ in length, fewer than 2 points remain,
	// or no intensity is > 0.
	[[nodiscard]] bool BuildExperimentalSpectrum(std::span<const double> x, std::span<const double> y,
		SpectrumAxisUnit unit, ExperimentalSpectrum &out, std::string &outError);

	// Energy of the largest intensity with minEv <= E <= maxEv (the experimental ZPL picked from a window);
	// NaN when no point lies in the window.
	[[nodiscard]] double SpectrumPeakEnergyEv(const ExperimentalSpectrum &spectrum, double minEv, double maxEv);
} // namespace DefectStudio
