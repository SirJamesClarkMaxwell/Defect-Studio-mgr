#include "Core/dspch.hpp"
#include "Domain/Electronic/HydrogenicOrbital.hpp"

namespace DefectStudio
{
	// STUB - see HydrogenicOrbital.cpp. tests/Domain/Electronic/OrbitalPresetTests.cpp is the
	// contract for everything in this file.

	OrbitalWavefunction MakeOrbitalPreset(
		OrbitalPreset /*preset*/, const OrbitalPresetSettings & /*settings*/)
	{
		return OrbitalWavefunction{};
	}

	const char *OrbitalPresetName(OrbitalPreset /*preset*/)
	{
		return "";
	}

	bool ParseOrbitalPreset(const std::string & /*name*/, OrbitalPreset & /*preset*/)
	{
		return false;
	}
} // namespace DefectStudio
