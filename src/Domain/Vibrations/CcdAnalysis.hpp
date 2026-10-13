#pragma once

#include <optional>
#include <string>
#include <vector>

namespace DefectStudio
{
	struct CcdPoint
	{
		std::string branch;
		double t = 0.0;
		double q = 0.0; // amu^0.5 Angstrom, gs minimum at zero
		std::optional<double> energyEv;
		std::string status;
	};

	struct CcdHarmonicWell
	{
		double hbarOmegaMeV = 0.0;
		double huangRhys = 0.0;
	};

	struct CcdAnalysisResult
	{
		std::string label;
		double deltaQ = 0.0;
		double zplEv = 0.0;
		std::string zplFormulaText;
		struct
		{
			std::optional<double> absorptionEv, emissionEv, relaxationGsEv, relaxationEsEv;
			std::string source;
		} franckCondon;
		struct { std::optional<CcdHarmonicWell> gs, es; } harmonic;
		std::vector<CcdPoint> points;
		struct { std::vector<double> q, vGsEv, vEsEv; } curves;
		struct { std::vector<double> gsMeV, esMeV; } levels; // relative to each bottom
		struct
		{
			std::vector<double> q;
			std::vector<std::vector<double>> gs, es; // indexed by n, normalized over q
		} wavefunctions;
		std::vector<double> fc0n; // optional ground-state FC weights, correlated with gs levels
		struct
		{
			std::vector<double> energyEv, numerov, harmonic, phonon1d; // unavailable columns are empty
		} pl1d;
	};
}
