#pragma once

namespace DefectStudio
{
	// Tessellation detail is quantised into power-of-two buckets of screen density so that a slow zoom
	// re-tessellates a handful of times, not every frame.
	inline constexpr int kMinLodBucket = -4;
	inline constexpr int kMaxLodBucket = 16;

	// Pass as `previousBucket` when there is no previous decision (first evaluation of a path).
	inline constexpr int kNoLodBucket = kMinLodBucket - 1;

	// Fraction of a bucket (in log2 density) the value must travel past a boundary before the bucket
	// changes. Guarantees a zoom sweep across one boundary switches at most once in each direction.
	inline constexpr double kLodHysteresis = 0.25;

	// bucket = clamp(floor(log2(pixelsPerWorldUnit)), kMin, kMax), with `previousBucket` retained while
	// the density stays inside its hysteresis band. Non-finite or non-positive density yields kMinLodBucket.
	[[nodiscard]] int QuantiseLod(double pixelsPerWorldUnit, int previousBucket);

	// World-space chord-height tolerance for a bucket: pixelErrorBudget pixels divided by the bucket's
	// density (2^bucket pixels per world unit). Always finite and > 0.
	[[nodiscard]] double ToleranceForLod(int bucket, double pixelErrorBudget);
}
