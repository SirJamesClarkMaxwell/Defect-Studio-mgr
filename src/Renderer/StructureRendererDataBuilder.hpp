#pragma once

#include <string>

#include "Core/Utils/Path.hpp"
#include "Domain/Crystal/CrystalStructure.hpp"
#include "Renderer/AtomStyleTable.hpp"
#include "Renderer/RendererTypes.hpp"

namespace DefectStudio
{
	// The 12 edges of the parallelepiped spanned by a lattice's three vectors. Public because a
	// window can carry a second, overlaid cell (RendererStructureData::overlayCellEdges).
	[[nodiscard]] std::vector<RendererCellEdge> BuildCellEdges(const glm::mat3 &lattice);

	[[nodiscard]] RendererStructureData BuildRendererStructureData(
		const CrystalStructure &structure,
		const Path &sourcePath,
		std::string name,
		const AtomStyleTable &atomStyleTable,
		std::string domainStructureId = {});
} // namespace DefectStudio
