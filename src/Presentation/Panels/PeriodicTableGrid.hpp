#pragma once

#include <array>

namespace DefectStudio
{
	// Standard periodic table layout (atomic number per cell, 0 = empty gap; lanthanides/actinides
	// listed separately below the main grid, same convention as RendererLayer::GetLanthanideSymbols/
	// GetActinideSymbols) - shared by every panel that draws a clickable periodic table
	// (RendererPanel::drawPeriodicTableWindow, ElementCatalogPanel) so this 7x18 layout only exists
	// once.
	using PeriodicTableIndexRow = std::array<int, 18>;
	inline constexpr std::array<PeriodicTableIndexRow, 7> kPeriodicTableElementIndices = {
		PeriodicTableIndexRow{1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2},
		PeriodicTableIndexRow{3, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 5, 6, 7, 8, 9, 10},
		PeriodicTableIndexRow{11, 12, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 13, 14, 15, 16, 17, 18},
		PeriodicTableIndexRow{19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36},
		PeriodicTableIndexRow{37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54},
		PeriodicTableIndexRow{55, 56, 0, 72, 73, 74, 75, 76, 77, 78, 79, 80, 81, 82, 83, 84, 85, 86},
		PeriodicTableIndexRow{87, 88, 0, 104, 105, 106, 107, 108, 109, 110, 111, 112, 113, 114, 115, 116, 117, 118}};
} // namespace DefectStudio
