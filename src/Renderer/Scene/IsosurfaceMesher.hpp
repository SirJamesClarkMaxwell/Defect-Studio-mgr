#pragma once

#include <cstdint>
#include <functional>
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

	struct IsosurfaceMeshOptions
	{
		// Analytic scene samples include both endpoints; imported periodic grids use i/N.
		bool endpointInclusive = false;
		bool smoothShading = true;
		// Optional continuous field: refine crossings and differentiate at the surface, rather
		// than interpolating coarse voxel gradients. Empty for imported calculation grids.
		std::function<float(const glm::vec3 &)> field;
	};

	struct IndexedIsosurfaceMesh
	{
		std::vector<IsosurfaceVertex> vertices;
		std::vector<std::uint32_t> indices;
	};

	// Shared grid-edge crossings, consistently outward winding for both signs. Smooth normals
	// come from the continuous field when provided, otherwise finite differences on the grid.
	[[nodiscard]] IndexedIsosurfaceMesh GenerateIndexedIsosurfaceMesh(
		const OrbitalGridData &grid, float isoValue, const IsosurfaceMeshOptions &options = {});

	// Compatibility boundary for the existing GL_TRIANGLES renderer and triangle-based picking.
	// Flat shading expands each face with its own normal; smooth shading expands welded vertices.
	[[nodiscard]] std::vector<IsosurfaceVertex> GenerateIsosurfaceMesh(
		const OrbitalGridData &grid, float isoValue, const IsosurfaceMeshOptions &options = {});
} // namespace DefectStudio
