#pragma once

#include <optional>
#include <string>
#include <vector>

#include "Core/Utils/Path.hpp"
#include "ScientificRuntime/Python/ScriptRunner.hpp"

namespace DefectStudio
{
	struct VaspOrbitalChannelData
	{
		float energy = 0.0f;
		float occupation = 0.0f;
		float localization = 0.0f;
		std::optional<std::string> irrep;
	};

	struct VaspOrbitalRecord
	{
		int band = 0;
		VaspOrbitalChannelData up;
		VaspOrbitalChannelData down;
	};

	struct VaspBandGapData
	{
		float bandgap = 0.0f;
		float homo = 0.0f;
		float lumo = 0.0f;
	};

	struct VaspOutputData
	{
		Path path;
		// nullopt if vasprun.xml is missing/unparsable for this calculation - not fatal, callers
		// should degrade gracefully (e.g. hide band-gap-relative UI, keep orbital data if present).
		std::optional<VaspBandGapData> gap;
		// nullopt if WAVECAR is missing - band gap data above may still be available without it.
		std::optional<std::vector<VaspOrbitalRecord>> orbitals;
		// Set only when orbitals is nullopt because WAVECAR exists but couldn't be read (corrupted/
		// incomplete transfer, seen on network drives) - distinguishes that case from WAVECAR simply
		// being absent, which leaves this nullopt too.
		std::optional<std::string> orbitalsError;
	};

	// Loads band-gap (HOMO/LUMO) and per-band orbital data (energy, occupation, localization
	// factor, irrep, both spin channels) for one VASP calculation directory, via puntukas'
	// VaspOutput (which itself reads OUTCAR/vasprun.xml/WAVECAR as available). Subprocess-only,
	// same rationale as PuntukasBridge: this is a low-frequency user action, not a hot loop.
	class VaspOutputBridge final
	{
	public:
		[[nodiscard]] Result<VaspOutputData> LoadOutput(
			const Path &directory,
			int bandStart = 0,
			int bandEnd = 10) const;

	private:
		ScriptRunner m_ScriptRunner;
	};
} // namespace DefectStudio
