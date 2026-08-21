#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "Core/Utils/Path.hpp"
#include "Renderer/RendererSettings.hpp"

namespace DefectStudio
{
	struct RendererAtomData
	{
		std::string element;
		glm::vec3 cartesianPosition = glm::vec3(0.0f, 0.0f, 0.0f);
		glm::vec3 color = glm::vec3(0.7f, 0.7f, 0.7f);
		float radius = 0.35f;
		bool visible = true;
	};

	struct RendererColorGradient
	{
		glm::vec3 start = glm::vec3(0.6f, 0.6f, 0.6f);
		glm::vec3 finish = glm::vec3(0.9f, 0.9f, 0.9f);
	};

	struct RendererBondData
	{
		std::uint32_t firstAtomIndex = 0;
		std::uint32_t secondAtomIndex = 0;
		float radius = 0.09f;
		RendererColorGradient gradient;
		// Cartesian offset to add to secondAtomIndex's position before computing bond geometry -
		// nonzero only for bonds that cross a periodic cell boundary (see Bond::periodicShift,
		// already resolved to Cartesian here so rendering doesn't need the lattice matrix again).
		glm::vec3 secondAtomPeriodicOffset = glm::vec3(0.0f);
	};

	struct RendererCellEdge
	{
		glm::vec3 start = glm::vec3(0.0f, 0.0f, 0.0f);
		glm::vec3 finish = glm::vec3(0.0f, 0.0f, 0.0f);
	};

	struct RendererStructureData
	{
		std::string domainStructureId;
		std::string name;
		Path sourcePath;
		std::vector<RendererAtomData> atoms;
		std::vector<RendererBondData> bonds;
		std::vector<RendererCellEdge> cellEdges;
		glm::mat3 lattice = glm::mat3(1.0f);
		glm::mat3 reciprocalLattice = glm::mat3(1.0f);
	};

	enum class SelectionToolMode
	{
		None,
		Box,
		Circle
	};

	struct RendererViewSnapshot
	{
		// Only meaningful for entries in RendererLayer's shared saved-views list; empty for
		// transient snapshots (undo/redo, align-axis, project default view).
		std::string name;
		glm::vec3 target = glm::vec3(0.0f);
		float distance = 1.0f;
		float yaw = 0.0f;
		float pitch = 0.0f;
		float roll = 0.0f;
		CameraProjection projection = CameraProjection::Perspective;
		// Indices are only meaningful within the structure they were captured from - valid for
		// same-window operations (undo/redo, align-axis, cycle-saved-view, ...) where before/after
		// always share one structure, and cheap for pushViewChange's before/after dedup compare.
		std::vector<std::size_t> selectedAtomIndices;
		std::vector<std::size_t> hiddenAtomIndices;
		// Positions of the same atoms, captured alongside the indices above. Restoring a snapshot
		// onto a *different* structure (the session default view copy/pasted between windows, or
		// auto-applied to a newly-opened window) re-resolves selection/visibility by nearest
		// position instead of reusing indices, since atom N in one structure is rarely atom N in
		// another. See SceneSystem::ResolveAtomIndicesByPosition.
		std::vector<glm::vec3> selectedAtomPositions;
		std::vector<glm::vec3> hiddenAtomPositions;
	};

	struct RendererViewStateChange
	{
		std::string description;
		RendererViewSnapshot before;
		RendererViewSnapshot after;
	};
} // namespace DefectStudio
