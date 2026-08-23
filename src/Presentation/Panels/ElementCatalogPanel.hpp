#pragma once

#include <string>

#include "Core/Utils/Path.hpp"
#include "Presentation/Panels/IPanel.hpp"
#include "Renderer/AtomStyleTable.hpp"
#include "Renderer/RendererLayer.hpp"

namespace DefectStudio
{
	class CommandRegistry;

	// Periodic-table element style picker: click an element to select it, edit its color/radius
	// below the grid. Edits commit through "renderer.selection.set_element_style" (undoable, Ctrl+Z)
	// and preview live in every open viewport while dragging - the drag itself only pokes the
	// already-built RendererAtomData/RendererBondData color/radius directly (see
	// RefreshOpenWindowsForElementStyle), leaving AtomStyleTable untouched until release, so the
	// commit command can still capture an accurate pre-drag value for Undo.
	class ElementCatalogPanel final : public IPanel
	{
	public:
		explicit ElementCatalogPanel(
			RendererLayer &layer,
			WeakRef<CommandRegistry> commandRegistry,
			AtomStyleTable atomStyleTable,
			Path atomStylesPath,
			std::string title = "Element Catalog",
			bool visibleByDefault = false);
		ElementCatalogPanel(const ElementCatalogPanel &other) = default;

		void Render() override;
		[[nodiscard]] Ref<IPanel> Clone() const override;

	private:
		void drawPeriodicTableGrid();
		void drawSelectedElementEditor();

		RendererLayer &m_Layer;
		WeakRef<CommandRegistry> m_CommandRegistry;
		AtomStyleTable m_AtomStyleTable;
		Path m_AtomStylesPath;
		std::string m_SelectedSymbol;
		// Snapshot of m_SelectedSymbol's style taken the instant a color/radius edit begins (Ctrl+Z
		// needs the value from *before* this drag, not whatever AtomStyleTable holds by the time the
		// drag ends - the live-preview refresh below never touches AtomStyleTable, so this is the only
		// place that value survives).
		AtomRenderStyle m_DragStartStyle;
		std::string m_StatusMessage;
	};
} // namespace DefectStudio
