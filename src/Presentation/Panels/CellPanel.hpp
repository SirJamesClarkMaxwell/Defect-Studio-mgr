#pragma once

#include <array>
#include <optional>

#include "Domain/Crystal/CellEditing.hpp"
#include "Domain/Crystal/ElementProperties.hpp"
#include "Presentation/Panels/IPanel.hpp"
#include "Renderer/AtomStyleTable.hpp"

namespace DefectStudio
{
	class CommandRegistry;
	class DomainLayer;
	class RendererLayer;

	class CellPanel final : public IPanel
	{
	public:
		// The tables open a new empty cell as a window (OpenCrystalStructureAsWindow), like SupercellBuilderPanel.
		CellPanel(RendererLayer &layer, WeakRef<CommandRegistry> commands, WeakRef<DomainLayer> domain,
			ElementPropertiesTable elements, AtomStyleTable styles, std::string title = "Komórka", bool visibleByDefault = false);

		void Render() override;
		[[nodiscard]] PanelCategory GetCategory() const override { return PanelCategory::Structure; }
		[[nodiscard]] Ref<IPanel> Clone() const override;

	private:
		bool replace(Result<CrystalStructure> structure, std::string description, bool changesSiteIndices = false);
		void drawEditor(const CrystalStructure &structure);
		void drawSublattice(const CrystalStructure &structure, int revision); // task/112d, CellPanelSublattice.cpp
		void drawNewCell();

		RendererLayer &m_Layer;
		WeakRef<CommandRegistry> m_Commands;
		WeakRef<DomainLayer> m_Domain;
		std::string m_WindowId;
		std::string m_StructureId;
		int m_Revision = -1;
		std::array<float, 6> m_Parameters = {};
		std::array<float, 6> m_DragParameters = {};
		std::array<int, 3> m_Repeat = {1, 1, 1};
		CellEditPositions m_Positions = CellEditPositions::KeepFractional;
		bool m_Editing = false;
		std::optional<std::size_t> m_Coincident;
		std::string m_Status;

		ElementPropertiesTable m_Elements;
		AtomStyleTable m_Styles;
		BravaisCenteringPreset m_Pattern = BravaisCenteringPreset::FaceCentered;
		std::string m_Species = "C";
		std::array<std::array<char, 24>, 3> m_Offset = {{{"0"}, {"0"}, {"0"}}};
		// Preview of the sublattice for the current inputs: recomputed only when this key changes.
		std::string m_PreviewKey;
		std::size_t m_PreviewNew = 0;
		std::size_t m_PreviewCoincident = 0;
		CrystalSystem m_NewSystem = CrystalSystem::Cubic;
		LatticeParameters m_NewParameters{5.43f, 5.43f, 5.43f, 90.0f, 90.0f, 90.0f};
	};
} // namespace DefectStudio
