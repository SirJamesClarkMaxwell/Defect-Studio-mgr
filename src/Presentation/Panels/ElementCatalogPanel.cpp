#include "Core/dspch.hpp"

#include "Presentation/Panels/ElementCatalogPanel.hpp"

#include <unordered_set>

#include <imgui.h>

#include "Core/Commands/CommandRegistry.hpp"
#include "Core/Logging/Logger.hpp"
#include "IO/AtomStyleIO.hpp"
#include "Presentation/Panels/PeriodicTableGrid.hpp"
#include "Renderer/Commands/RendererAtomEditCommands.hpp"

namespace DefectStudio
{
	namespace
	{
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

		// Classic textbook periodic-table grouping, derived from atomic number alone - approximate
		// for a few contested/superheavy elements (e.g. At is grouped with the metalloids rather than
		// the halogens here; boundaries above Z=112 are sparse and rarely relevant to a defect
		// structure tool), good enough for a visual reference coloring, not a chemistry claim.
		[[nodiscard]] ElementCategory ClassifyElement(int z)
		{
			static const std::unordered_set<int> kAlkali = {3, 11, 19, 37, 55, 87};
			static const std::unordered_set<int> kAlkalineEarth = {4, 12, 20, 38, 56, 88};
			static const std::unordered_set<int> kMetalloid = {5, 14, 32, 33, 51, 52, 85};
			static const std::unordered_set<int> kHalogen = {9, 17, 35, 53, 117};
			static const std::unordered_set<int> kNobleGas = {2, 10, 18, 36, 54, 86, 118};
			static const std::unordered_set<int> kPostTransition = {13, 31, 49, 50, 81, 82, 83, 84, 113, 114, 115, 116};
			static const std::unordered_set<int> kOtherNonmetal = {1, 6, 7, 8, 15, 16, 34};

			if (z >= 57 && z <= 71)
				return ElementCategory::Lanthanide;
			if (z >= 89 && z <= 103)
				return ElementCategory::Actinide;
			if (kAlkali.contains(z))
				return ElementCategory::AlkaliMetal;
			if (kAlkalineEarth.contains(z))
				return ElementCategory::AlkalineEarthMetal;
			if (kNobleGas.contains(z))
				return ElementCategory::NobleGas;
			if (kHalogen.contains(z))
				return ElementCategory::Halogen;
			if (kMetalloid.contains(z))
				return ElementCategory::Metalloid;
			if (kOtherNonmetal.contains(z))
				return ElementCategory::Nonmetal;
			if (kPostTransition.contains(z))
				return ElementCategory::PostTransitionMetal;
			if ((z >= 21 && z <= 30) || (z >= 39 && z <= 48) || (z >= 72 && z <= 80) || (z >= 104 && z <= 112))
				return ElementCategory::TransitionMetal;
			return ElementCategory::Unknown;
		}

		[[nodiscard]] glm::vec3 CategoryColor(ElementCategory category)
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
	} // namespace

	ElementCatalogPanel::ElementCatalogPanel(
		RendererLayer &layer,
		WeakRef<CommandRegistry> commandRegistry,
		AtomStyleTable atomStyleTable,
		ElementPropertiesTable elementPropertiesTable,
		Path atomStylesPath,
		std::string title,
		bool visibleByDefault)
		: IPanel(std::move(title), visibleByDefault),
		  m_Layer(layer),
		  m_CommandRegistry(std::move(commandRegistry)),
		  m_AtomStyleTable(std::move(atomStyleTable)),
		  m_ElementPropertiesTable(std::move(elementPropertiesTable)),
		  m_AtomStylesPath(std::move(atomStylesPath))
	{
	}

	Ref<IPanel> ElementCatalogPanel::Clone() const
	{
		return CreateRef<ElementCatalogPanel>(*this);
	}

	void ElementCatalogPanel::drawSelectedElementEditor()
	{
		if (m_SelectedSymbol.empty())
		{
			ImGui::TextDisabled("Click an element above to edit its style.");
			return;
		}

		// Reseed only on an actual selection change - see m_LiveStyle's declaration for why not
		// every frame.
		if (m_LiveStyleSymbol != m_SelectedSymbol)
		{
			m_LiveStyle = m_AtomStyleTable.GetStyle(m_SelectedSymbol);
			m_LiveStyleSymbol = m_SelectedSymbol;
		}

		ImGui::Text("Element: %s", m_SelectedSymbol.c_str());

		bool changed = false;

		ImGui::SetNextItemWidth(200.0f);
		changed |= ImGui::ColorEdit3("Color", &m_LiveStyle.color.x);
		if (ImGui::IsItemActivated())
			m_DragStartStyle = m_AtomStyleTable.GetStyle(m_SelectedSymbol);
		const bool colorCommitted = ImGui::IsItemDeactivatedAfterEdit();

		ImGui::SetNextItemWidth(200.0f);
		changed |= ImGui::DragFloat("Radius", &m_LiveStyle.displayRadius, 0.01f, 0.05f, 3.0f, "%.2f");
		if (ImGui::IsItemActivated())
			m_DragStartStyle = m_AtomStyleTable.GetStyle(m_SelectedSymbol);
		const bool radiusCommitted = ImGui::IsItemDeactivatedAfterEdit();

		// Live preview: pokes the already-built render-side atom/bond color+radius directly, every
		// frame the value changes (including mid-drag) - AtomStyleTable itself stays untouched until
		// commit below, so Undo can still recover the true pre-drag value from m_DragStartStyle.
		if (changed)
			RefreshOpenWindowsForElementStyle(m_Layer, m_SelectedSymbol, m_LiveStyle);

		if (colorCommitted || radiusCommitted)
		{
			Ref<CommandRegistry> commandRegistry = m_CommandRegistry.lock();
			if (commandRegistry != nullptr)
			{
				SetElementStylePayload payload;
				payload.symbol = m_SelectedSymbol;
				payload.previousStyle = m_DragStartStyle;
				payload.newStyle = m_LiveStyle;
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
				else
					m_LiveStyle = AtomRenderStyle{};
			}
		}

		const ElementProperties &properties = m_ElementPropertiesTable.Get(m_SelectedSymbol);
		ImGui::Separator();
		ImGui::Text("Atomic number: %d", properties.atomicNumber);
		ImGui::Text("Atomic mass: %.3f u", properties.mass);
		ImGui::Text("Covalent radius: %.3f A", properties.covalentRadius);
		ImGui::Text("Van der Waals radius: %.3f A", properties.vanDerWaalsRadius);
	}

	void ElementCatalogPanel::Render()
	{
		if (!IsVisible())
			return;

		ImGui::SetNextWindowSize(ImVec2(640.0f, 620.0f), ImGuiCond_FirstUseEver);
		bool windowOpen = true;
		if (!ImGui::Begin(GetTitle().c_str(), &windowOpen))
		{
			ImGui::End();
			SetVisible(windowOpen);
			return;
		}

		if (ImGui::RadioButton("Style color", m_ColorMode == ElementColorMode::Style))
			m_ColorMode = ElementColorMode::Style;
		ImGui::SameLine();
		if (ImGui::RadioButton("Category", m_ColorMode == ElementColorMode::Category))
			m_ColorMode = ElementColorMode::Category;

		const std::string clicked = DrawPeriodicTableGrid(
			m_Layer,
			[&](const std::string &symbol) -> glm::vec3
			{
				if (m_ColorMode == ElementColorMode::Category)
					return CategoryColor(ClassifyElement(m_ElementPropertiesTable.Get(symbol).atomicNumber));
				return m_AtomStyleTable.GetStyle(symbol).color;
			},
			m_SelectedSymbol, ImVec2(40.0f, 34.0f));
		if (!clicked.empty())
			m_SelectedSymbol = clicked;

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
