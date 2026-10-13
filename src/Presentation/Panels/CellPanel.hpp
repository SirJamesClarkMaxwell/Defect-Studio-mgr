#pragma once

#include <array>
#include <optional>

#include "Domain/Crystal/CellEditing.hpp"
#include "Presentation/Panels/IPanel.hpp"

namespace DefectStudio
{
	class CommandRegistry;
	class DomainLayer;
	class RendererLayer;

	class CellPanel final : public IPanel
	{
	public:
		explicit CellPanel(RendererLayer &layer, WeakRef<CommandRegistry> commands, WeakRef<DomainLayer> domain,
			std::string title = "Komórka", bool visibleByDefault = false);

		void Render() override;
		[[nodiscard]] PanelCategory GetCategory() const override { return PanelCategory::Structure; }
		[[nodiscard]] Ref<IPanel> Clone() const override;

	private:
		bool replace(Result<CrystalStructure> structure, std::string description, bool changesSiteIndices = false);
		void drawEditor(const CrystalStructure &structure);

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
	};
} // namespace DefectStudio
