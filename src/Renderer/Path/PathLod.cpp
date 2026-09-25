#include "Core/dspch.hpp"

#include "Renderer/Path/PathLod.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace DefectStudio
{
	int QuantiseLod(double pixelsPerWorldUnit, int previousBucket)
	{
		if (!std::isfinite(pixelsPerWorldUnit) || pixelsPerWorldUnit <= 0.0)
			return kMinLodBucket;
		const double logarithm = std::log2(pixelsPerWorldUnit);
		if (previousBucket >= kMinLodBucket && previousBucket <= kMaxLodBucket &&
			logarithm >= static_cast<double>(previousBucket) - kLodHysteresis &&
			logarithm < static_cast<double>(previousBucket + 1) + kLodHysteresis)
			return previousBucket;
		return std::clamp(static_cast<int>(std::floor(logarithm)), kMinLodBucket, kMaxLodBucket);
	}

	double ToleranceForLod(int bucket, double pixelErrorBudget)
	{
		const int clampedBucket = std::clamp(bucket, kMinLodBucket, kMaxLodBucket);
		const double density = std::ldexp(1.0, clampedBucket);
		const double budget = std::isfinite(pixelErrorBudget) && pixelErrorBudget > 0.0 ? pixelErrorBudget : 1.0;
		const double maximum = std::numeric_limits<double>::max() / density;
		return std::max(std::numeric_limits<double>::min(), std::min(budget, maximum) / density);
	}
} // namespace DefectStudio
