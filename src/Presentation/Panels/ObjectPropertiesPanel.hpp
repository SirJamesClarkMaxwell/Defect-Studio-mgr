#pragma once

#include <array>
#include <string>

#include <optional>

#include "Presentation/Panels/IPanel.hpp"
#include "Renderer/AtomStyleTable.hpp"
#include "Renderer/RendererLayer.hpp"

namespace DefectStudio
{
	class CommandRegistry;
	class DomainLayer;

	// The panel's body without its ImGui window - drawn both by ObjectPropertiesPanel and by the
	// viewport's N side panel (ViewportSidePanel.hpp), which must show the same widgets, not copies.
	// `styles` (shared AtomStyleTable data) enables the vacancy style editor; null hides it.
	void DrawObjectPropertiesContent(
		RendererLayer &layer, const WeakRef<CommandRegistry> &commandRegistryRef,
		const WeakRef<DomainLayer> &domainLayerRef, AtomStyleTable *styles = nullptr);

	// Properties of the single selected atom in the focused renderer viewport. Cartesian/fractional
	// position (translate X/Y/Z, kept in sync via CrystalStructure::CartesianToFractional/
	// FractionalToCartesian) and element both commit through the same commands the viewport gizmo
	// drag and context-menu "change type" already use, so undo/redo behaves identically either way.
	// Label/charge/magnetization/occupancy/selective dynamics are domain-only AtomSite fields with
	// no renderer-side representation - read via ResolveAtomEditTarget (RendererAtomEditCommands.hpp)
	// straight from the live domain structure rather than from RendererAtomData, and committed
	// through "renderer.selection.set_atom_properties". Multi-select editing (docs/work/project/
	// TODO.md "Replanning 2026-08-22") is a separate, later task - not designed here.
	class ObjectPropertiesPanel final : public IPanel
	{
	public:
		explicit ObjectPropertiesPanel(
			RendererLayer &layer,
			WeakRef<CommandRegistry> commandRegistry,
			WeakRef<DomainLayer> domainLayer,
			std::optional<AtomStyleTable> atomStyleTable = std::nullopt,
			std::string title = "Object Properties",
			bool visibleByDefault = false);
		ObjectPropertiesPanel(const ObjectPropertiesPanel &other) = default;

		void Render() override;
		[[nodiscard]] PanelCategory GetCategory() const override { return PanelCategory::Scene; }
		[[nodiscard]] Ref<IPanel> Clone() const override;

	private:
		RendererLayer &m_Layer;
		WeakRef<CommandRegistry> m_CommandRegistry;
		WeakRef<DomainLayer> m_DomainLayer;
		std::optional<AtomStyleTable> m_AtomStyleTable;
	};
} // namespace DefectStudio
