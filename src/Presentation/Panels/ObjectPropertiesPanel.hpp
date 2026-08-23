#pragma once

#include <array>
#include <string>

#include "Presentation/Panels/IPanel.hpp"
#include "Renderer/RendererLayer.hpp"

namespace DefectStudio
{
	class CommandRegistry;
	class DomainLayer;

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
			std::string title = "Object Properties",
			bool visibleByDefault = false);
		ObjectPropertiesPanel(const ObjectPropertiesPanel &other) = default;

		void Render() override;
		[[nodiscard]] Ref<IPanel> Clone() const override;

	private:
		RendererLayer &m_Layer;
		WeakRef<CommandRegistry> m_CommandRegistry;
		WeakRef<DomainLayer> m_DomainLayer;
	};
} // namespace DefectStudio
