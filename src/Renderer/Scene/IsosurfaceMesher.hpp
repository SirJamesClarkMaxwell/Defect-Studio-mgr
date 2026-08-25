#pragma once

#include <vector>

#include <glm/glm.hpp>

#include "Domain/Electronic/ElectronicStructureModel.hpp"

namespace DefectStudio
{
	struct IsosurfaceVertex
	{
		glm::vec3 position = glm::vec3(0.0f);
		glm::vec3 normal = glm::vec3(0.0f, 1.0f, 0.0f);
		// +1 for the positive-value lobe, -1 for the negative-value lobe - lets the renderer
		// color the two phases of the wavefunction independently.
		float sign = 1.0f;
	};

	// CPU marching-tetrahedra isosurface extraction (6-tet cube decomposition, not marching
	// cubes - a much smaller/safer case table: 16 entries instead of 256, at the cost of more
	// triangles per cell). Reference implementation to validate the data pipeline and algorithm;
	// the compute shader (isosurface_march.comp) is the one actually used for rendering/export and
	// has since diverged in one respect - it uses interpolated density-gradient normals instead of
	// this file's flat per-triangle normal, which is what actually renders smoothly. Case-table and
	// vertex positions are still kept identical between the two.
	//
	// Extracts BOTH lobes of the signed wavefunction grid in one pass: the positive-value
	// surface at +isoValue and the negative-value surface at -isoValue (isoValue must be > 0,
	// otherwise returns empty). Output is flat GL_TRIANGLES triplets (every 3 consecutive
	// vertices is one triangle, flat-shaded, no vertex welding).
	[[nodiscard]] std::vector<IsosurfaceVertex> GenerateIsosurfaceMesh(
		const OrbitalGridData &grid, float isoValue);
} // namespace DefectStudio
