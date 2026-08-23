#include "Core/dspch.hpp"

#include "Presentation/Panels/ElementCatalogPanel.hpp"

#include <algorithm>

#include <imgui.h>

#include "Core/Commands/CommandRegistry.hpp"
#include "Core/Logging/Logger.hpp"
#include "IO/AtomStyleIO.hpp"
#include "Presentation/Panels/PeriodicTableGrid.hpp"
#include "Renderer/Commands/RendererAtomEditCommands.hpp"

namespace DefectStudio
{
	ElementCatalogPanel::ElementCatalogPanel(
		RendererLayer &layer,
		WeakRef<CommandRegistry> commandRegistry,
		AtomStyleTable atomStyleTable,
		Path atomStylesPath,
		std::string title,
		bool visibleByDefault)
		: IPanel(std::move(title), visibleByDefault),
		  m_Layer(layer),
		  m_CommandRegistry(std::move(commandRegistry)),
		  m_AtomStyleTable(std::move(atomStyleTable)),
		  m_AtomStylesPath(std::move(atomStylesPath))
	{
	}

	Ref<IPanel> ElementCatalogPanel::Clone() const
	{
		return CreateRef<ElementCatalogPanel>(*this);
	}

	void ElementCatalogPanel::drawPeriodicTableGrid()
	{
		const ImVec2 cellSize(32.0f, 28.0f);

		auto drawCell = [&](const std::string &symbol)
		{
			const AtomRenderStyle style = m_AtomStyleTable.GetStyle(symbol);
			const ImVec4 color(style.color.x, style.color.y, style.color.z, 1.0f);
			ImGui::PushStyleColor(ImGuiCol_Button, color);
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, color);
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, color);
			const bool clicked = ImGui::Button(symbol.c_str(), cellSize);
			ImGui::PopStyleColor(3);
			if (symbol == m_SelectedSymbol)
				ImGui::GetWindowDrawList()->AddRect(
					ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), IM_COL32(255, 215, 0, 255), 0.0f, 0, 2.5f);
			if (clicked)
				m_SelectedSymbol = symbol;
		};

		const auto &symbols = m_Layer.GetPeriodicTableSymbols();
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
		const auto &lanthanides = m_Layer.GetLanthanideSymbols();
		ImGui::TextUnformatted("Lanthanides");
		ImGui::SameLine();
		for (std::size_t index = 0; index < lanthanides.size(); ++index)
		{
			if (index > 0)
				ImGui::SameLine();
			drawCell(lanthanides[index]);
		}
		const auto &actinides = m_Layer.GetActinideSymbols();
		ImGui::TextUnformatted("Actinides  ");
		ImGui::SameLine();
		for (std::size_t index = 0; index < actinides.size(); ++index)
		{
			if (index > 0)
				ImGui::SameLine();
			drawCell(actinides[index]);
		}
	}

	void ElementCatalogPanel::drawSelectedElementEditor()
	{
		if (m_SelectedSymbol.empty())
		{
			ImGui::TextDisabled("Click an element above to edit its style.");
			return;
		}

		ImGui::Text("Element: %s", m_SelectedSymbol.c_str());

		AtomRenderStyle current = m_AtomStyleTable.GetStyle(m_SelectedSymbol);
		bool changed = false;

		ImGui::SetNextItemWidth(200.0f);
		changed |= ImGui::ColorEdit3("Color", &current.color.x);
		if (ImGui::IsItemActivated())
			m_DragStartStyle = m_AtomStyleTable.GetStyle(m_SelectedSymbol);
		const bool colorCommitted = ImGui::IsItemDeactivatedAfterEdit();

		ImGui::SetNextItemWidth(200.0f);
		changed |= ImGui::DragFloat("Radius", &current.displayRadius, 0.01f, 0.05f, 3.0f, "%.2f");
		if (ImGui::IsItemActivated())
			m_DragStartStyle = m_AtomStyleTable.GetStyle(m_SelectedSymbol);
		const bool radiusCommitted = ImGui::IsItemDeactivatedAfterEdit();

		// Live preview: pokes the already-built render-side atom/bond color+radius directly, every
		// frame the value changes (including mid-drag) - AtomStyleTable itself stays untouched until
		// commit below, so Undo can still recover the true pre-drag value from m_DragStartStyle.
		if (changed)
			RefreshOpenWindowsForElementStyle(m_Layer, m_SelectedSymbol, current);

		if (colorCommitted || radiusCommitted)
		{
			Ref<CommandRegistry> commandRegistry = m_CommandRegistry.lock();
			if (commandRegistry != nullptr)
			{
				SetElementStylePayload payload;
				payload.symbol = m_SelectedSymbol;
				payload.previousStyle = m_DragStartStyle;
				payload.newStyle = current;
				CommandContext context;
				context.Set<SetElementStylePayload>("atom_edit.set_element_style_payload", std::move(payload));
				Result<CommandOutcome> result =
					commandRegistry->Execute(CommandID{"renderer.selection.set_element_style"}, std::move(context));
				if (!result)
					DS_LOG_WARN("Set element style failed: {}", result.Error().technicalDetails);
			}
		}

		if (ImGui::Button("Reset to default"))
		{
			Ref<CommandRegistry> commandRegistry = m_CommandRegistry.lock();
			if (commandRegistry != nullptr)
			{
				SetElementStylePayload payload;
				payload.symbol = m_SelectedSymbol;
				payload.previousStyle = m_AtomStyleTable.GetStyle(m_SelectedSymbol);
				payload.newStyle = AtomRenderStyle{};
				CommandContext context;
				context.Set<SetElementStylePayload>("atom_edit.set_element_style_payload", std::move(payload));
				Result<CommandOutcome> result =
					commandRegistry->Execute(CommandID{"renderer.selection.set_element_style"}, std::move(context));
				if (!result)
					DS_LOG_WARN("Reset element style failed: {}", result.Error().technicalDetails);
			}
		}
	}

	void ElementCatalogPanel::Render()
	{
		if (!IsVisible())
			return;

		bool windowOpen = true;
		if (!ImGui::Begin(GetTitle().c_str(), &windowOpen))
		{
			ImGui::End();
			SetVisible(windowOpen);
			return;
		}

		drawPeriodicTableGrid();
		ImGui::Separator();
		drawSelectedElementEditor();

		ImGui::Separator();
		if (ImGui::Button("Save to file"))
		{
			std::string error;
			if (AtomStyleIO::SaveToFile(m_AtomStylesPath, m_AtomStyleTable.AllStyles(), m_AtomStyleTable.GetVacancyStyle(), error))
				m_StatusMessage = "Saved to " + m_AtomStylesPath.String();
			else
				m_StatusMessage = "Save failed: " + error;
		}
		if (!m_StatusMessage.empty())
			ImGui::TextDisabled("%s", m_StatusMessage.c_str());

		ImGui::End();
		SetVisible(windowOpen);
	}
} // namespace DefectStudio
