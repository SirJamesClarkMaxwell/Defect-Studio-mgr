#include "Core/dspch.hpp"

#include "Presentation/Panels/PeriodicTableGrid.hpp"

#include <unordered_set>

#include "Renderer/RendererLayer.hpp"

namespace DefectStudio
{
	// Classic textbook periodic-table grouping, derived from atomic number alone - approximate for a
	// few contested/superheavy elements (e.g. At is grouped with the metalloids rather than the
	// halogens here; boundaries above Z=112 are sparse and rarely relevant to a defect structure
	// tool), good enough for a visual reference coloring, not a chemistry claim.
	ElementCategory ClassifyElement(int atomicNumber)
	{
		static const std::unordered_set<int> kAlkali = {3, 11, 19, 37, 55, 87};
		static const std::unordered_set<int> kAlkalineEarth = {4, 12, 20, 38, 56, 88};
		static const std::unordered_set<int> kMetalloid = {5, 14, 32, 33, 51, 52, 85};
		static const std::unordered_set<int> kHalogen = {9, 17, 35, 53, 117};
		static const std::unordered_set<int> kNobleGas = {2, 10, 18, 36, 54, 86, 118};
		static const std::unordered_set<int> kPostTransition = {13, 31, 49, 50, 81, 82, 83, 84, 113, 114, 115, 116};
		static const std::unordered_set<int> kOtherNonmetal = {1, 6, 7, 8, 15, 16, 34};

		if (atomicNumber >= 57 && atomicNumber <= 71)
			return ElementCategory::Lanthanide;
		if (atomicNumber >= 89 && atomicNumber <= 103)
			return ElementCategory::Actinide;
		if (kAlkali.contains(atomicNumber))
			return ElementCategory::AlkaliMetal;
		if (kAlkalineEarth.contains(atomicNumber))
			return ElementCategory::AlkalineEarthMetal;
		if (kNobleGas.contains(atomicNumber))
			return ElementCategory::NobleGas;
		if (kHalogen.contains(atomicNumber))
			return ElementCategory::Halogen;
		if (kMetalloid.contains(atomicNumber))
			return ElementCategory::Metalloid;
		if (kOtherNonmetal.contains(atomicNumber))
			return ElementCategory::Nonmetal;
		if (kPostTransition.contains(atomicNumber))
			return ElementCategory::PostTransitionMetal;
		if ((atomicNumber >= 21 && atomicNumber <= 30) || (atomicNumber >= 39 && atomicNumber <= 48) ||
			(atomicNumber >= 72 && atomicNumber <= 80) || (atomicNumber >= 104 && atomicNumber <= 112))
			return ElementCategory::TransitionMetal;
		return ElementCategory::Unknown;
	}

	glm::vec3 CategoryColor(ElementCategory category)
	{
		switch (category)
		{
			case ElementCategory::AlkaliMetal:
				return glm::vec3(1.00f, 0.60f, 0.60f);
			case ElementCategory::AlkalineEarthMetal:
				return glm::vec3(1.00f, 0.80f, 0.45f);
			case ElementCategory::TransitionMetal:
				return glm::vec3(1.00f, 0.90f, 0.55f);
			case ElementCategory::PostTransitionMetal:
				return glm::vec3(0.55f, 0.70f, 0.55f);
			case ElementCategory::Metalloid:
				return glm::vec3(0.55f, 0.75f, 0.75f);
			case ElementCategory::Nonmetal:
				return glm::vec3(0.55f, 0.85f, 0.55f);
			case ElementCategory::Halogen:
				return glm::vec3(0.85f, 0.90f, 0.45f);
			case ElementCategory::NobleGas:
				return glm::vec3(0.70f, 0.55f, 0.90f);
			case ElementCategory::Lanthanide:
				return glm::vec3(0.70f, 0.85f, 0.95f);
			case ElementCategory::Actinide:
				return glm::vec3(0.95f, 0.65f, 0.85f);
			default:
				return glm::vec3(0.55f, 0.55f, 0.55f);
		}
	}

	int AtomicNumberForSymbol(RendererLayer &layer, const std::string &symbol)
	{
		const auto &symbols = layer.GetPeriodicTableSymbols();
		for (std::size_t i = 0; i < symbols.size(); ++i)
		{
			if (symbols[i] == symbol)
				return static_cast<int>(i) + 1;
		}
		return 0;
	}

	std::string DrawPeriodicTableGrid(
		RendererLayer &layer,
		const std::function<glm::vec3(const std::string &)> &colorForSymbol,
		const std::string &selectedSymbol,
		ImVec2 cellSize)
	{
		std::string clickedSymbol;

		// A few extra pixels of breathing room between cells - added on top of whatever the app's
		// current style already has (rather than a hardcoded value) so a later global style tweak
		// doesn't fight this. Popped right before returning below.
		ImVec2 spacing = ImGui::GetStyle().ItemSpacing;
		spacing.x += 3.0f;
		spacing.y += 3.0f;
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, spacing);

		auto drawCell = [&](const std::string &symbol)
		{
			const glm::vec3 styleColor = colorForSymbol(symbol);
			const ImVec4 color(styleColor.x, styleColor.y, styleColor.z, 1.0f);
			// Perceived luminance (Rec. 601) picks black or white text - a light background (pale
			// category colors, or a light custom atom style) with ImGui's default light text was
			// unreadable.
			const float luminance = 0.299f * styleColor.x + 0.587f * styleColor.y + 0.114f * styleColor.z;
			const ImVec4 textColor = luminance > 0.6f ? ImVec4(0.05f, 0.05f, 0.05f, 1.0f) : ImVec4(0.95f, 0.95f, 0.95f, 1.0f);
			ImGui::PushStyleColor(ImGuiCol_Button, color);
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, color);
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, color);
			ImGui::PushStyleColor(ImGuiCol_Text, textColor);
			const bool clicked = ImGui::Button(symbol.c_str(), cellSize);
			ImGui::PopStyleColor(4);
			if (symbol == selectedSymbol)
				ImGui::GetWindowDrawList()->AddRect(
					ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), IM_COL32(255, 215, 0, 255), 0.0f, 0, 2.5f);
			if (clicked)
				clickedSymbol = symbol;
		};

		const auto &symbols = layer.GetPeriodicTableSymbols();
		for (const PeriodicTableIndexRow &row : kPeriodicTableElementIndices)
		{
			for (std::size_t column = 0; column < row.size(); ++column)
			{
				const int atomicNumber = row[column];
				if (column > 0)
					ImGui::SameLine();
				if (atomicNumber <= 0 || static_cast<std::size_t>(atomicNumber) > symbols.size())
				{
					ImGui::Dummy(cellSize);
					continue;
				}
				drawCell(symbols[static_cast<std::size_t>(atomicNumber - 1)]);
			}
		}

		ImGui::Separator();
		const auto &lanthanides = layer.GetLanthanideSymbols();
		ImGui::TextUnformatted("Lanthanides");
		ImGui::SameLine();
		for (std::size_t index = 0; index < lanthanides.size(); ++index)
		{
			if (index > 0)
				ImGui::SameLine();
			drawCell(lanthanides[index]);
		}
		const auto &actinides = layer.GetActinideSymbols();
		ImGui::TextUnformatted("Actinides  ");
		ImGui::SameLine();
		for (std::size_t index = 0; index < actinides.size(); ++index)
		{
			if (index > 0)
				ImGui::SameLine();
			drawCell(actinides[index]);
		}

		ImGui::PopStyleVar();
		return clickedSymbol;
	}
} // namespace DefectStudio
