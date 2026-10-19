#pragma once

#include <string>
#include <vector>
#include <glm/glm.hpp>

#include "Core/Utils/Path.hpp"

namespace DefectStudio
{
	struct PhononDispersionTick { double distance = 0.0; std::string label; };
	struct PhononDispersion
	{
		int bandCount = 0;
		std::vector<int> segment;                 // per q-point
		std::vector<double> distance;             // per q-point, non-decreasing
		std::vector<std::vector<double>> bandsMeV; // [band][q-point]
		std::vector<PhononDispersionTick> ticks;
		std::vector<double> breaks;
		double minMeV = 0.0, maxMeV = 0.0;
		bool inPlane = false;
		std::vector<glm::dvec3> qpoints;
	};

	namespace PhononDispersionIO
	{
		// Reads summary.json (ds.phonon_dispersion.v1) + dispersion.tsv. False + error on missing
		// files, wrong schema, column count != bandCount + 2, non-finite or invalid path values.
		// Failure leaves out unchanged; success clears error.
		[[nodiscard]] bool Load(const Path &directory, PhononDispersion &out, std::string &error);
	}
}
