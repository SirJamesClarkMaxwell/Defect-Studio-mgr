#pragma once

#include <array>
#include <functional>
#include <string>

#include <glm/glm.hpp>
#include <imgui.h>

namespace DefectStudio
{
	class RendererLayer;

	// Standard periodic table layout (atomic number per cell, 0 = empty gap; lanthanides/actinides
	// listed separately below the main grid, same convention as RendererLayer::GetLanthanideSymbols/
	// GetActinideSymbols) - shared by every panel that draws a clickable periodic table
	// (RendererPanel::drawAddAtomPopup/drawPeriodicTableWindow, ElementCatalogPanel) so this 7x18
	// layout only exists once.
	using PeriodicTableIndexRow = std::array<int, 18>;
	inline constexpr std::array<PeriodicTableIndexRow, 7> kPeriodicTableElementIndices = {
		PeriodicTableIndexRow{1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2},
		PeriodicTableIndexRow{3, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 5, 6, 7, 8, 9, 10},
		PeriodicTableIndexRow{11, 12, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 13, 14, 15, 16, 17, 18},
		PeriodicTableIndexRow{19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36},
		PeriodicTableIndexRow{37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54},
		PeriodicTableIndexRow{55, 56, 0, 72, 73, 74, 75, 76, 77, 78, 79, 80, 81, 82, 83, 84, 85, 86},
		PeriodicTableIndexRow{87, 88, 0, 104, 105, 106, 107, 108, 109, 110, 111, 112, 113, 114, 115, 116, 117, 118}};

	// Draws the grid plus lanthanide/actinide rows below it, one button per element colored by
	// colorForSymbol - callers decide what a color means (current AtomStyleTable style, category
	// classification, or just a flat neutral fill for a plain element picker). selectedSymbol (may
	// be empty) gets a highlighted border. Returns the symbol clicked this frame, or an empty string
	// if none was. outDoubleClickedSymbol (optional) receives the symbol double-clicked this frame -
	// checked via IsItemHovered()+IsMouseDoubleClicked() rather than the click return above, since
	// IsMouseDoubleClicked fires on the second click's mouse-DOWN while Button's own return fires on
	// mouse-UP; the two are never true on the same frame, so a caller wanting "double-click to
	// confirm" needs this rather than combining the click return with its own IsMouseDoubleClicked.
	[[nodiscard]] std::string DrawPeriodicTableGrid(
		RendererLayer &layer,
		const std::function<glm::vec3(const std::string &)> &colorForSymbol,
		const std::string &selectedSymbol,
		ImVec2 cellSize = ImVec2(38.0f, 32.0f),
		std::string *outDoubleClickedSymbol = nullptr);

	// Classic textbook periodic-table category, shared by every panel that colors elements by
	// category (ElementCatalogPanel, RendererPanel::drawPeriodicTableWindow) so this classification
	// only exists once - see PeriodicTableGrid.cpp for the exact groupings and their caveats.
	enum class ElementCategory
	{
		AlkaliMetal,
		AlkalineEarthMetal,
		TransitionMetal,
		PostTransitionMetal,
		Metalloid,
		Nonmetal,
		Halogen,
		NobleGas,
		Lanthanide,
		Actinide,
		Unknown
	};
	[[nodiscard]] ElementCategory ClassifyElement(int atomicNumber);
	[[nodiscard]] glm::vec3 CategoryColor(ElementCategory category);

	// Reverse lookup for callers (like drawPeriodicTableWindow) that only have a symbol on hand, not
	// an atomic number - linear scan over GetPeriodicTableSymbols() (118 entries, only called per
	// grid cell while a periodic table window is open, not a hot path). Returns 0 if not found.
	[[nodiscard]] int AtomicNumberForSymbol(RendererLayer &layer, const std::string &symbol);
} // namespace DefectStudio
