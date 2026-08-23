#pragma once

#include <string>
#include <vector>

#include "Domain/Crystal/CrystalPrimitives.hpp"
#include "Domain/Crystal/ElementProperties.hpp"
#include "Presentation/Panels/IPanel.hpp"
#include "Renderer/RendererLayer.hpp"

namespace DefectStudio
{
	class CommandRegistry;
	class DomainLayer;

	// Edits the focused viewport's BondGenerationSettings (global cutoff scale + per-pair overrides,
	// CrystalPrimitives.hpp) and triggers a rebuild through "renderer.bonds.set_settings" - the same
	// settings/regeneration logic already ran automatically after every atom add/delete/type-change
	// (BondGenerator.cpp), just with no way to see or change the cutoffs it uses, or to force a
	// rebuild on demand.
	class BondSettingsPanel final : public IPanel
	{
	public:
		explicit BondSettingsPanel(
			RendererLayer &layer,
			WeakRef<CommandRegistry> commandRegistry,
			WeakRef<DomainLayer> domainLayer,
			ElementPropertiesTable elementPropertiesTable,
			std::string title = "Bond Settings",
			bool visibleByDefault = false);
		BondSettingsPanel(const BondSettingsPanel &other) = default;

		void Render() override;
		[[nodiscard]] Ref<IPanel> Clone() const override;

	private:
		void applySettings();
		void drawPairPickerPopup(const char *popupId, char *targetBuffer, std::size_t targetSize);

		RendererLayer &m_Layer;
		WeakRef<CommandRegistry> m_CommandRegistry;
		WeakRef<DomainLayer> m_DomainLayer;
		ElementPropertiesTable m_ElementPropertiesTable;

		// Local edit buffer, reseeded from the live structure whenever the target window changes -
		// not written back until "Rebuild bonds" runs (see Element Catalog's m_LiveStyle for the same
		// reasoning: fighting a live authoritative value every frame breaks ImGui's own drag/edit
		// tracking, and here there's no live-preview requirement to justify pushing every keystroke
		// through an undoable command anyway).
		std::string m_EditedForWindowId;
		BondGenerationSettings m_EditedSettings;

		// When true (default), any edit to m_EditedSettings immediately calls applySettings() instead
		// of waiting for the "Rebuild bonds" button - that button still exists to force a rebuild on
		// demand (e.g. after external structure changes) even with auto-rebuild on.
		bool m_AutoRebuild = true;

		// Add-new-pair-override row (not part of BondGenerationSettings itself until "Add" commits it
		// into m_EditedSettings.perPairCutoffOverride).
		char m_NewPairFirst[8] = "";
		char m_NewPairSecond[8] = "";
		float m_NewPairScale = 1.18f;
		std::string m_StatusMessage;
	};
} // namespace DefectStudio
