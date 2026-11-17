#include "Core/dspch.hpp"
#include "Domain/Vibrations/ExperimentalSpectrum.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace DefectStudio
{
	double ToElectronVolt(double value, SpectrumAxisUnit unit)
	{
		switch (unit)
		{
		case SpectrumAxisUnit::ElectronVolt:
			return value;
		}
		return std::numeric_limits<double>::quiet_NaN();
	}

	bool BuildExperimentalSpectrum(std::span<const double> x, std::span<const double> y,
		SpectrumAxisUnit unit, ExperimentalSpectrum &out, std::string &outError)
	{
		outError.clear();
		if (x.size() != y.size())
		{
			outError = "Spectrum axis and intensity lengths differ";
			return false;
		}
		std::vector<std::pair<double, double>> points;
		points.reserve(x.size());
		for (std::size_t i = 0; i < x.size(); ++i)
		{
			if (!std::isfinite(x[i]) || !std::isfinite(y[i]))
				continue;
			const double energy = ToElectronVolt(x[i], unit);
			if (std::isfinite(energy))
				points.emplace_back(energy, y[i]);
		}
		std::stable_sort(points.begin(), points.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
		ExperimentalSpectrum result;
		std::size_t count = 0;
		for (const auto &[energy, intensity] : points)
		{
			if (result.energyEv.empty() || result.energyEv.back() != energy)
			{
				result.energyEv.push_back(energy);
				result.intensity.push_back(intensity);
				count = 1;
			}
			else
			{
				++count;
				// Interpolation avoids overflowing a sum of finite measured intensities.
				result.intensity.back() = std::lerp(result.intensity.back(), intensity, 1.0 / static_cast<double>(count));
			}
		}
		if (result.energyEv.size() < 2)
		{
			outError = "Spectrum needs at least two distinct finite energies";
			return false;
		}
		if (std::none_of(result.intensity.begin(), result.intensity.end(), [](double value) { return value > 0.0; }))
		{
			outError = "Spectrum needs a positive intensity";
			return false;
		}
		out = std::move(result);
		return true;
	}

	double SpectrumPeakEnergyEv(const ExperimentalSpectrum &spectrum, double minEv, double maxEv)
	{
		double peak = std::numeric_limits<double>::quiet_NaN();
		double maximum = -std::numeric_limits<double>::infinity();
		if (spectrum.energyEv.size() != spectrum.intensity.size())
			return peak;
		for (std::size_t i = 0; i < spectrum.energyEv.size(); ++i)
		{
			const double energy = spectrum.energyEv[i];
			const double intensity = spectrum.intensity[i];
			if (std::isfinite(energy) && std::isfinite(intensity) && energy >= minEv && energy <= maxEv && intensity > maximum)
			{
				maximum = intensity;
				peak = energy;
			}
		}
		return peak;
	}
} // namespace DefectStudio
