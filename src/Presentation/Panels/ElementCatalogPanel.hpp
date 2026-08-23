#pragma once

#include <string>

#include "Core/Utils/Path.hpp"
#include "Domain/Crystal/ElementProperties.hpp"
#include "Presentation/Panels/IPanel.hpp"
#include "Renderer/AtomStyleTable.hpp"
#include "Renderer/RendererLayer.hpp"

namespace DefectStudio
{
	class CommandRegistry;

	// How the periodic table grid colors each cell - Style mirrors the live AtomStyleTable (so the
	// grid doubles as "what does my structure actually look like"), Category is the classic
	// textbook alkali/noble-gas/etc. coloring, independent of any per-element style customization.
	enum class ElementColorMode
	{
		Style,
		Category
	};

	// Periodic-table element style picker: click an element to select it, edit its color/radius
	// below the grid, browse its other properties (mass, covalent/van der Waals radius - from
	// ElementPropertiesTable, the same data BondGenerator's auto-bond cutoff uses). Edits commit
	// through "renderer.selection.set_element_style" (undoable, Ctrl+Z) and preview live in every
	// open viewport while dragging - the drag itself only pokes the already-built RendererAtomData/
	// RendererBondData color/radius directly (see RefreshOpenWindowsForElementStyle), leaving
	// AtomStyleTable untouched until release, so the commit command can still capture an accurate
	// pre-drag value for Undo.
	class ElementCatalogPanel final : public IPanel
	{
	public:
		explicit ElementCatalogPanel(
			RendererLayer &layer,
			WeakRef<CommandRegistry> commandRegistry,
			AtomStyleTable atomStyleTable,
			ElementPropertiesTable elementPropertiesTable,
			Path atomStylesPath,
			std::string title = "Element Catalog",
			bool visibleByDefault = false);
		ElementCatalogPanel(const ElementCatalogPanel &other) = default;

		void Render() override;
		[[nodiscard]] Ref<IPanel> Clone() const override;

	private:
		void drawSelectedElementEditor();

		RendererLayer &m_Layer;
		WeakRef<CommandRegistry> m_CommandRegistry;
		AtomStyleTable m_AtomStyleTable;
		ElementPropertiesTable m_ElementPropertiesTable;
		Path m_AtomStylesPath;
		ElementColorMode m_ColorMode = ElementColorMode::Style;
		std::string m_SelectedSymbol;
		// Persistent across frames while m_SelectedSymbol is unchanged, NOT re-read from
		// AtomStyleTable every frame - ColorEdit3/DragFloat's own internal drag state expects to feed
		// back into the same value it's editing; re-seeding from the (deliberately untouched-during-
		// drag) AtomStyleTable each frame fought the widget's own in-progress delta and produced a
		// visible flicker/snap-back. Reseeded only on selection change (see drawSelectedElementEditor).
		AtomRenderStyle m_LiveStyle;
		std::string m_LiveStyleSymbol;
		// Snapshot of m_SelectedSymbol's style taken the instant a color/radius edit begins (Ctrl+Z
		// needs the value from *before* this drag, not whatever AtomStyleTable holds by the time the
		// drag ends - the live-preview refresh never touches AtomStyleTable, so this is the only place
		// that value survives).
		AtomRenderStyle m_DragStartStyle;
		std::string m_StatusMessage;
	};
} // namespace DefectStudio
