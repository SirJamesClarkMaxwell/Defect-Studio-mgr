#include "Core/dspch.hpp"

#include "Domain/Defects/FnvCorrection.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace DefectStudio
{
	namespace
	{
		void ValidateAxis(const FnvAxisProfile &axis, const std::vector<double> &model)
		{
			if (!std::isfinite(axis.length) || axis.length <= 0.0 || axis.position.empty() ||
				axis.position.size() != axis.deltaV.size() || axis.position.size() != model.size())
				throw std::invalid_argument("FNV requires a positive axis length and matching nonempty profiles.");
			for (std::size_t i = 0; i < model.size(); ++i)
				if (!std::isfinite(axis.position[i]) || !std::isfinite(axis.deltaV[i]) || !std::isfinite(model[i]))
					throw std::invalid_argument("FNV profiles must contain finite values.");
		}

		[[nodiscard]] double Wrap(const double position, const double length)
		{
			const double wrapped = std::fmod(position, length);
			return wrapped < 0.0 ? wrapped + length : wrapped;
		}

		[[nodiscard]] double PeriodicDistance(const double position, const double centre, const double length)
		{
			return std::abs(Wrap(position - centre + length / 2.0, length) - length / 2.0);
		}

		[[nodiscard]] FnvAxisAlignment DescribeAxis(const FnvAxisProfile &axis, const std::vector<double> &model)
		{
			FnvAxisAlignment result;
			const auto peak = std::max_element(model.begin(), model.end(),
				[](const double a, const double b) { return std::abs(a) < std::abs(b); });
			result.defectPosition = axis.position[static_cast<std::size_t>(peak - model.begin())];

			std::size_t count = 0;
			double modelMean = 0.0, deltaMean = 0.0, variance = 0.0, covariance = 0.0;
			for (std::size_t i = 0; i < model.size(); ++i)
			{
				if (PeriodicDistance(axis.position[i], result.defectPosition, axis.length) <= kFnvSlopeCoreRadius)
					continue;
				++count;
				const double dx = model[i] - modelMean;
				const double dy = axis.deltaV[i] - deltaMean;
				modelMean += dx / static_cast<double>(count);
				deltaMean += dy / static_cast<double>(count);
				variance += dx * (model[i] - modelMean);
				covariance += dx * (axis.deltaV[i] - deltaMean);
			}
			result.slope = variance > 0.0 ? covariance / variance : 0.0;
			return result;
		}

		// Shared population statistics without allocating residual or mask arrays.
		template <typename Inside>
		[[nodiscard]] bool AlignResidual(const FnvAxisProfile &axis, const std::vector<double> &model,
			FnvAxisAlignment &result, const Inside &inside)
		{
			std::size_t count = 0;
			double mean = 0.0, sumSquares = 0.0;
			for (std::size_t i = 0; i < model.size(); ++i)
			{
				if (!inside(axis.position[i]))
					continue;
				++count;
				const double residual = axis.deltaV[i] - model[i];
				const double delta = residual - mean;
				mean += delta / static_cast<double>(count);
				sumSquares += delta * (residual - mean);
			}
			result.alignment = mean;
			result.spread = count > 0 ? std::sqrt(std::max(0.0, sumSquares / static_cast<double>(count))) : 0.0;
			return count > 0;
		}
	} // namespace

	std::vector<double> FnvModelPotential(const FnvProfiles &profiles, const int axis, const FnvModelConvention convention)
	{
		if (axis < 0 || axis >= static_cast<int>(profiles.axes.size()))
			throw std::out_of_range("FNV axis must be 0, 1 or 2.");
		std::vector<double> model = profiles.axes[axis].modelPotential;
		if (convention == FnvModelConvention::Sxdefectalign)
		{
			const double shift = (profiles.dielectric - 1.0) * profiles.modelPotentialMean;
			for (double &value : model)
				value += shift;
		}
		return model;
	}

	FnvAxisAlignment ProposeFnvAlignment(const FnvAxisProfile &axis, const std::vector<double> &modelPotential,
		const double minimumWidth, const double tolerance)
	{
		ValidateAxis(axis, modelPotential);
		if (axis.position.size() < 2 || !std::isfinite(minimumWidth) || minimumWidth <= 0.0 ||
			!std::isfinite(tolerance) || tolerance < 0.0)
			throw std::invalid_argument("FNV proposal requires two grid points, a positive width and a nonnegative tolerance.");
		const double dx = axis.position[1] - axis.position[0];
		if (!std::isfinite(dx) || dx <= 0.0)
			throw std::invalid_argument("FNV grid spacing must be positive and finite.");

		FnvAxisAlignment result = DescribeAxis(axis, modelPotential);
		result.window = {Wrap(result.defectPosition + axis.length / 2.0, axis.length), minimumWidth};
		const auto align = [&](FnvAxisAlignment &candidate, const double width)
		{
			return AlignResidual(axis, modelPotential, candidate, [&](const double position)
			{
				return PeriodicDistance(position, result.defectPosition, axis.length) >= axis.length / 2.0 - width / 2.0;
			});
		};
		align(result, minimumWidth);
		result.flat = false;
		for (std::size_t step = 0; ; ++step)
		{
			const double width = minimumWidth + static_cast<double>(step) * dx;
			if (width > axis.length / 2.0 + 1e-9)
				break;
			FnvAxisAlignment candidate = result;
			if (align(candidate, width) && candidate.spread <= tolerance)
			{
				candidate.window.width = width;
				candidate.flat = true;
				result = candidate;
			}
		}
		return result;
	}

	FnvAxisAlignment AlignFnvInWindow(const FnvAxisProfile &axis, const std::vector<double> &modelPotential,
		const FnvPlateauWindow &window)
	{
		ValidateAxis(axis, modelPotential);
		if (!std::isfinite(window.centre) || !std::isfinite(window.width) || window.width < 0.0)
			throw std::invalid_argument("FNV window centre and width must be finite, with a nonnegative width.");
		FnvAxisAlignment result = DescribeAxis(axis, modelPotential);
		result.window = window;
		result.flat = AlignResidual(axis, modelPotential, result, [&](const double position)
		{
			return PeriodicDistance(position, window.centre, axis.length) <= window.width / 2.0 + 1e-12;
		}) && result.spread <= kFnvPlateauTolerance;
		return result;
	}

	FnvCorrection CombineFnvCorrection(const FnvProfiles &profiles, const std::array<FnvAxisAlignment, 3> &alignments,
		const std::array<bool, 3> &useAxis)
	{
		const bool useAll = std::none_of(useAxis.begin(), useAxis.end(), [](const bool use) { return use; });
		FnvCorrection result;
		result.latticeEnergy = profiles.latticeEnergy;
		double minimum = 0.0, maximum = 0.0;
		int count = 0;
		for (std::size_t a = 0; a < alignments.size(); ++a)
		{
			if (!useAll && !useAxis[a])
				continue;
			const double alignment = alignments[a].alignment;
			if (count == 0)
				minimum = maximum = alignment;
			minimum = std::min(minimum, alignment);
			maximum = std::max(maximum, alignment);
			result.alignment += alignment;
			++count;
		}
		result.alignment /= static_cast<double>(count);
		result.alignmentSpread = maximum - minimum;
		result.correction = result.latticeEnergy + profiles.charge * result.alignment;
		return result;
	}
} // namespace DefectStudio
