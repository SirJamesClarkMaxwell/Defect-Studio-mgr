#include "Core/dspch.hpp"
#include "Domain/Electronic/HydrogenicOrbital.hpp"

namespace DefectStudio
{
	// STUB - the analytic hydrogenic wavefunction is not implemented yet. The header and
	// tests/Domain/Electronic/HydrogenicOrbitalTests.cpp are the contract; every function below
	// returns a neutral value so the test binary links and the tests fail on the physics rather
	// than on the linker.

	float HydrogenicRadial(int /*n*/, int /*l*/, float /*effectiveCharge*/, float /*radius*/)
	{
		return 0.0f;
	}

	float RealSphericalHarmonic(int /*l*/, int /*m*/, const glm::vec3 & /*offset*/)
	{
		return 0.0f;
	}

	float EvaluateAtomicOrbital(const AtomicOrbital & /*orbital*/, const glm::vec3 & /*offset*/)
	{
		return 0.0f;
	}

	float EvaluateOrbital(const OrbitalWavefunction & /*wavefunction*/, const glm::vec3 & /*point*/)
	{
		return 0.0f;
	}

	float SuggestOrbitalExtent(const OrbitalWavefunction & /*wavefunction*/)
	{
		return 0.0f;
	}

	glm::vec3 OrbitalCentroid(const OrbitalWavefunction & /*wavefunction*/)
	{
		return glm::vec3(0.0f);
	}

	OrbitalGridData SampleOrbitalToGrid(
		const OrbitalWavefunction & /*wavefunction*/, const OrbitalSamplingSettings & /*settings*/)
	{
		return OrbitalGridData{};
	}

	float SuggestOrbitalIsoValue(const OrbitalGridData & /*grid*/, float /*fractionOfPeak*/)
	{
		return 0.0f;
	}
} // namespace DefectStudio
