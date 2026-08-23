#pragma once

#include <string>
#include <vector>

#include "Domain/Crystal/CrystalPrimitives.hpp"
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
			std::string title = "Bond Settings",
			bool visibleByDefault = false);
		BondSettingsPanel(const BondSettingsPanel &other) = default;

		void Render() override;
		[[nodiscard]] Ref<IPanel> Clone() const override;

	private:
		void applySettings();

		RendererLayer &m_Layer;
		WeakRef<CommandRegistry> m_CommandRegistry;
		WeakRef<DomainLayer> m_DomainLayer;

		// Local edit buffer, reseeded from the live structure whenever the target window changes -
		// not written back until "Rebuild bonds" runs (see Element Catalog's m_LiveStyle for the same
		// reasoning: fighting a live authoritative value every frame breaks ImGui's own drag/edit
		// tracking, and here there's no live-preview requirement to justify pushing every keystroke
		// through an undoable command anyway).
		std::string m_EditedForWindowId;
		BondGenerationSettings m_EditedSettings;

		// Add-new-pair-override row (not part of BondGenerationSettings itself until "Add" commits it
		// into m_EditedSettings.perPairCutoffOverride).
		char m_NewPairFirst[8] = "";
		char m_NewPairSecond[8] = "";
		float m_NewPairScale = 1.18f;
		std::string m_StatusMessage;
	};
} // namespace DefectStudio
