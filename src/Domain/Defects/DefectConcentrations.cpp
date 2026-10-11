#include "Core/dspch.hpp"

#include "Domain/Defects/DefectConcentrations.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>
#include <numeric>
#include <utility>

namespace DefectStudio
{
	namespace
	{
		constexpr double kLogZero = -std::numeric_limits<double>::infinity();
		constexpr double kResidualTolerance = 1e-10;

		// Computes log(exp(a) + exp(b)) without overflowing or losing an underflowed density.
		double LogAdd(double a, double b)
		{
			if (a == kLogZero)
				return b;
			if (b == kLogZero)
				return a;
			return std::max(a, b) + std::log1p(std::exp(-std::abs(a - b)));
		}

		// Computes log(1/(1+exp(x))) for electron occupation; holes use the opposite x.
		double LogOccupation(double x)
		{
			return -std::max(x, 0.0) - std::log1p(std::exp(-std::abs(x)));
		}

		// Computes log of the normalized F_1/2(eta) = 2/sqrt(pi)*integral sqrt(x)*f(x-eta) dx.
		// Simpson quadrature uses x=u^2 to remove the endpoint singularity, with a 40 kT tail.
		// Outside [-16,64], the fugacity series / Sommerfeld expansion avoids extreme ranges.
		// Definition and limits: NIST Fermi-Dirac Integral; see the task/100 design note.
		double LogFermiHalf(double eta)
		{
			const double pi = std::numbers::pi;
			if (eta < -16.0)
			{
				const double z = std::exp(eta);
				return eta + std::log1p(-z / std::sqrt(8.0) + z * z / std::sqrt(27.0));
			}
			if (eta > 64.0)
			{
				const double r = pi * pi / (eta * eta);
				return std::log(4.0 / (3.0 * std::sqrt(pi))) + 1.5 * std::log(eta) +
					std::log1p(r / 8.0 + 7.0 * r * r / 640.0 + 31.0 * r * r * r / 3072.0);
			}
			constexpr int intervals = 1024;
			const double step = std::sqrt(std::max(eta, 0.0) + 40.0) / intervals;
			double sum = 0.0;
			for (int i = 1; i <= intervals; ++i)
			{
				const double u = i * step;
				const double weight = i == intervals ? 1.0 : (i % 2 == 0 ? 2.0 : 4.0);
				sum += weight * u * u * std::exp(LogOccupation(u * u - eta));
			}
			return std::log(4.0 / std::sqrt(pi) * step / 3.0 * sum);
		}

		// Computes log(N_c or N_v) in cm^-3 from the aggregate DOS mass / m_0 and T in kelvin.
		double LogEffectiveDensity(double mass, double temperatureK)
		{
			constexpr double electronMassKg = 9.1093837139e-31;
			constexpr double boltzmannJPerK = 1.380649e-23;
			constexpr double planckJs = 6.62607015e-34;
			return std::log(2.0e-6) + 1.5 * (std::log(2.0 * std::numbers::pi * electronMassKg *
				boltzmannJPerK / (planckJs * planckJs)) + std::log(mass) + std::log(temperatureK));
		}

		// Computes log(integral DOS(E)*f((E-E_F)/kT) dE) in cm^-3 for a linear segment.
		// Filled regions integrate exactly; eight-point Gauss-Legendre panels <= 2 kT resolve the
		// edge. Truncating each segment's empty tail at 40 kT retains exponentially small carriers.
		double LogDosSegment(double from, double to, double densityFrom, double densityTo,
			double fermiLevelEv, double kT)
		{
			const auto density = [&](double energy)
			{
				return std::lerp(densityFrom, densityTo, std::clamp((energy - from) / (to - from), 0.0, 1.0));
			};
			const double filledEnd = std::clamp(fermiLevelEv - 40.0 * kT, from, to);
			double integral = kLogZero;
			if (filledEnd > from)
			{
				const double average = std::midpoint(densityFrom, density(filledEnd));
				if (average > 0.0)
					integral = std::log(filledEnd - from) + std::log(average);
			}
			const double end = std::min(to, std::max(from, fermiLevelEv) + 40.0 * kT);
			if (end <= filledEnd)
				return integral;
			constexpr std::array<double, 4> nodes{0.1834346424956498, 0.5255324099163290,
				0.7966664774136267, 0.9602898564975363};
			constexpr std::array<double, 4> weights{0.3626837833783620, 0.3137066458778873,
				0.2223810344533745, 0.1012285362903763};
			const int panels = static_cast<int>(std::ceil((end - filledEnd) / (2.0 * kT)));
			const double width = (end - filledEnd) / panels;
			for (int panel = 0; panel < panels; ++panel)
			{
				const double center = filledEnd + (panel + 0.5) * width;
				for (std::size_t i = 0; i < nodes.size(); ++i)
					for (double sign : {-1.0, 1.0})
					{
						const double energy = center + sign * nodes[i] * width / 2.0;
						const double dos = density(energy);
						if (dos > 0.0)
							integral = LogAdd(integral, std::log(width / 2.0) + std::log(weights[i]) +
								std::log(dos) + LogOccupation((energy - fermiLevelEv) / kT));
					}
			}
			return integral;
		}

		// Computes carrier log densities over the finite supplied DOS. Reflecting valence energies
		// E -> -E and E_F -> -E_F gives holes directly, avoiding subtraction of occupation from 1.
		double LogDosCarrier(std::span<const DosPoint> dos, double fermiLevelEv, double kT, bool holes)
		{
			double integral = kLogZero;
			for (std::size_t i = 1; i < dos.size(); ++i)
			{
				const auto &a = dos[i - 1];
				const auto &b = dos[i];
				integral = LogAdd(integral, holes ?
					LogDosSegment(-b.energyEv, -a.energyEv, b.statesPerEvCm3, a.statesPerEvCm3, -fermiLevelEv, kT) :
					LogDosSegment(a.energyEv, b.energyEv, a.statesPerEvCm3, b.statesPerEvCm3, fermiLevelEv, kT));
			}
			return integral;
		}

		// Validates a finite, ordered, nonnegative DOS with nonzero band weight and no gap states.
		bool ValidDos(std::span<const DosPoint> dos, double gapEv, bool valence)
		{
			if (dos.size() < 2)
				return false;
			bool nonzero = false;
			for (std::size_t i = 0; i < dos.size(); ++i)
			{
				const auto &point = dos[i];
				if (!std::isfinite(point.energyEv) || !std::isfinite(point.statesPerEvCm3) ||
					point.statesPerEvCm3 < 0.0 || (valence ? point.energyEv > 0.0 : point.energyEv < gapEv) ||
					(i > 0 && (point.energyEv <= dos[i - 1].energyEv ||
						!std::isfinite(point.energyEv - dos[i - 1].energyEv))))
					return false;
				nonzero = nonzero || point.statesPerEvCm3 > 0.0;
			}
			return nonzero;
		}

		// Validates the site density and every intercept/degeneracy before any exponential is used.
		bool ValidDefect(const ConcentrationDefect &defect)
		{
			return !defect.defect.empty() && !defect.chargeStates.empty() &&
				std::isfinite(defect.siteDensityCm3) && defect.siteDensityCm3 >= 0.0 &&
				std::all_of(defect.chargeStates.begin(), defect.chargeStates.end(), [](const auto &state)
				{
					return std::isfinite(state.interceptEv) && std::isfinite(state.degeneracy) && state.degeneracy > 0.0;
				});
		}

		// Converts log(cm^-3) to a representable density; underflow is zero, overflow is an error.
		bool DensityFromLog(double logarithm, double &density)
		{
			if (logarithm > std::log(std::numeric_limits<double>::max()) || std::isnan(logarithm))
				return false;
			density = std::exp(logarithm);
			return std::isfinite(density);
		}
	} // namespace

	ConcentrationDefects BuildConcentrationDefects(std::span<const ConcentrationDefectInput> inputs,
		const FormationReference &reference, const SpeciesEnergies &absoluteMu)
	{
		ConcentrationDefects result;
		for (const auto &input : inputs)
		{
			if (input.chargeStates.empty() || (!input.degeneracies.empty() &&
				input.degeneracies.size() != input.chargeStates.size()))
				return {{}, "Empty defect charge states or mismatched degeneracies"};
			ConcentrationDefect defect{input.chargeStates.front().defect, input.siteDensityCm3, {}};
			for (std::size_t i = 0; i < input.chargeStates.size(); ++i)
			{
				const auto &state = input.chargeStates[i];
				const auto intercept = FormationEnergy(state, reference, absoluteMu, 0.0);
				if (!intercept || state.defect != defect.defect)
					return {{}, "Missing/invalid chemical potential or inconsistent defect name"};
				defect.chargeStates.push_back({state.charge, *intercept,
					input.degeneracies.empty() ? 1.0 : input.degeneracies[i]});
			}
			if (!ValidDefect(defect))
				return {{}, "Invalid defect site density, intercept or degeneracy"};
			result.defects.push_back(std::move(defect));
		}
		return result;
	}

	DefectConcentrationSolution SolveDefectConcentrations(std::span<const ConcentrationDefect> defects,
		const ConcentrationCarrierBands &bands, double gapEv, double temperatureK, double fixedChargeCm3)
	{
		DefectConcentrationSolution result;
		result.temperatureK = temperatureK;
		result.fixedChargeCm3 = fixedChargeCm3;
		if (!std::isfinite(temperatureK) || temperatureK < 1.0 || temperatureK > 3000.0 ||
			!std::isfinite(gapEv) || gapEv <= 0.0 || !std::isfinite(fixedChargeCm3) ||
			!std::all_of(defects.begin(), defects.end(), ValidDefect))
		{
			result.error = "Invalid temperature (1..3000 K), gap, fixed charge or defect inputs";
			return result;
		}
		const double kT = kBoltzmannEvPerK * temperatureK;
		double logNc = 0.0;
		double logNv = 0.0;
		if (std::holds_alternative<EffectiveMassBands>(bands))
		{
			const auto &mass = std::get<EffectiveMassBands>(bands);
			if (!std::isfinite(mass.electronDosMass) || mass.electronDosMass <= 0.0 ||
				!std::isfinite(mass.holeDosMass) || mass.holeDosMass <= 0.0)
			{
				result.error = "DOS effective masses must be finite and positive";
				return result;
			}
			logNc = LogEffectiveDensity(mass.electronDosMass, temperatureK);
			logNv = LogEffectiveDensity(mass.holeDosMass, temperatureK);
		}
		else
		{
			const auto &dos = std::get<TabulatedDosBands>(bands);
			if (!ValidDos(dos.valence, gapEv, true) || !ValidDos(dos.conduction, gapEv, false))
			{
				result.error = "DOS bands need ordered finite samples, nonnegative/nonzero weight and no gap states";
				return result;
			}
		}

		std::vector<double> defectLogs;
		std::vector<double> chargeLogs;
		std::vector<int> signs;
		for (std::size_t i = 0; i < defects.size(); ++i)
			for (std::size_t j = 0; j < defects[i].chargeStates.size(); ++j)
				result.chargeStates.push_back({i, j, defects[i].chargeStates[j].charge, 0.0});
		defectLogs.resize(result.chargeStates.size());
		chargeLogs.resize(result.chargeStates.size() + 3);
		signs.resize(chargeLogs.size());

		// Returns signed neutrality / largest charge term, including terms below double underflow.
		const auto residual = [&](double fermiLevelEv)
		{
			std::size_t index = 0;
			for (const auto &defect : defects)
				for (const auto &state : defect.chargeStates)
				{
					defectLogs[index] = defect.siteDensityCm3 == 0.0 ? kLogZero :
						std::log(defect.siteDensityCm3) + std::log(state.degeneracy) -
						(state.interceptEv + static_cast<double>(state.charge) * fermiLevelEv) / kT;
					chargeLogs[index] = state.charge == 0 ? kLogZero :
						defectLogs[index] + std::log(std::abs(static_cast<double>(state.charge)));
					signs[index++] = (state.charge > 0) - (state.charge < 0);
				}
			if (std::holds_alternative<EffectiveMassBands>(bands))
			{
				chargeLogs[index] = logNv + LogFermiHalf(-fermiLevelEv / kT);
				chargeLogs[index + 1] = logNc + LogFermiHalf((fermiLevelEv - gapEv) / kT);
			}
			else
			{
				const auto &dos = std::get<TabulatedDosBands>(bands);
				chargeLogs[index] = LogDosCarrier(dos.valence, fermiLevelEv, kT, true);
				chargeLogs[index + 1] = LogDosCarrier(dos.conduction, fermiLevelEv, kT, false);
			}
			signs[index] = 1;
			signs[index + 1] = -1;
			chargeLogs[index + 2] = fixedChargeCm3 == 0.0 ? kLogZero : std::log(std::abs(fixedChargeCm3));
			signs[index + 2] = (fixedChargeCm3 > 0.0) - (fixedChargeCm3 < 0.0);
			const double scale = *std::max_element(chargeLogs.begin(), chargeLogs.end());
			if (!std::isfinite(scale))
				return std::numeric_limits<double>::quiet_NaN();
			long double sum = 0.0;
			for (std::size_t i = 0; i < chargeLogs.size(); ++i)
				sum += signs[i] * std::exp(chargeLogs[i] - scale);
			return static_cast<double>(sum);
		};

		double left = -1.0;
		double right = gapEv + 1.0;
		double leftResidual = residual(left);
		double rightResidual = residual(right);
		for (int expansion = 0; expansion < 64 && (leftResidual < 0.0 || rightResidual > 0.0); ++expansion)
		{
			const double width = right - left;
			if (leftResidual < 0.0)
			{
				left -= width;
				leftResidual = residual(left);
			}
			if (rightResidual > 0.0)
			{
				right += width;
				rightResidual = residual(right);
			}
		}
		if (!std::isfinite(leftResidual) || !std::isfinite(rightResidual) || leftResidual < 0.0 || rightResidual > 0.0)
		{
			result.error = "Could not bracket charge neutrality (DOS may not supply enough carriers)";
			return result;
		}
		for (int iteration = 0; iteration < 256; ++iteration)
		{
			result.fermiLevelEv = std::midpoint(left, right);
			result.relativeNeutralityResidual = residual(result.fermiLevelEv);
			if (!std::isfinite(result.relativeNeutralityResidual))
				break;
			if (std::abs(result.relativeNeutralityResidual) <= kResidualTolerance)
			{
				result.converged = true;
				break;
			}
			if (result.fermiLevelEv == left || result.fermiLevelEv == right)
				break;
			if (result.relativeNeutralityResidual > 0.0)
				left = result.fermiLevelEv;
			else
				right = result.fermiLevelEv;
		}
		if (!result.converged)
		{
			result.error = "Charge neutrality did not reach the relative residual tolerance";
			return result;
		}

		std::vector<double> totalLogs(defects.size(), kLogZero);
		bool representable = true;
		for (std::size_t i = 0; i < result.chargeStates.size(); ++i)
		{
			representable &= DensityFromLog(defectLogs[i], result.chargeStates[i].concentrationCm3);
			const auto defectIndex = result.chargeStates[i].defectIndex;
			totalLogs[defectIndex] = LogAdd(totalLogs[defectIndex], defectLogs[i]);
		}
		result.defectTotalsCm3.resize(defects.size());
		for (std::size_t i = 0; i < defects.size(); ++i)
			representable &= DensityFromLog(totalLogs[i], result.defectTotalsCm3[i]);
		representable &= DensityFromLog(chargeLogs[defectLogs.size()], result.holesCm3);
		representable &= DensityFromLog(chargeLogs[defectLogs.size() + 1], result.electronsCm3);
		if (!representable)
		{
			result.converged = false;
			result.error = "Concentration exceeds the finite double range; dilute model may be invalid";
		}
		return result;
	}

	std::vector<DefectConcentrationGridPoint> SolveDefectConcentrationGrid(
		std::span<const ConcentrationDefectInput> defects, const FormationReference &reference,
		const ConcentrationCarrierBands &bands, std::span<const double> temperaturesK,
		std::span<const SpeciesEnergies> absoluteMuSets, std::span<const double> fixedChargesCm3)
	{
		std::vector<DefectConcentrationGridPoint> points;
		for (std::size_t t = 0; t < temperaturesK.size(); ++t)
			for (std::size_t mu = 0; mu < absoluteMuSets.size(); ++mu)
			{
				const auto prepared = BuildConcentrationDefects(defects, reference, absoluteMuSets[mu]);
				for (std::size_t fixed = 0; fixed < fixedChargesCm3.size(); ++fixed)
				{
					DefectConcentrationSolution solution;
					if (prepared.error.empty())
						solution = SolveDefectConcentrations(prepared.defects, bands, reference.gap,
							temperaturesK[t], fixedChargesCm3[fixed]);
					else
					{
						solution.temperatureK = temperaturesK[t];
						solution.fixedChargeCm3 = fixedChargesCm3[fixed];
						solution.error = prepared.error;
					}
					points.push_back({t, mu, fixed, std::move(solution)});
				}
			}
		return points;
	}
} // namespace DefectStudio
