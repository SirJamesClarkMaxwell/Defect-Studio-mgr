#include "Core/dspch.hpp"

#include "Presentation/Panels/PeriodicTableGrid.hpp"

#include "Renderer/RendererLayer.hpp"

namespace DefectStudio
{
	std::string DrawPeriodicTableGrid(
		RendererLayer &layer,
		const std::function<glm::vec3(const std::string &)> &colorForSymbol,
		const std::string &selectedSymbol,
		ImVec2 cellSize)
	{
		std::string clickedSymbol;

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

		return clickedSymbol;
	}
} // namespace DefectStudio
