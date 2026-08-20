#include "Core/dspch.hpp"

#include "Presentation/Panels/ElectronicStructurePanel.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

#include <imgui.h>

#include "Core/Logging/Logger.hpp"
#include "Core/Platform/FileDialog.hpp"

namespace DefectStudio
{
	namespace
	{
		// Derives the highest band index that still counts as occupied from already-fetched data -
		// puntukas exposes HOMO/LUMO as energies only, not a band index, so this is our own reading
		// of the same "occ > threshold" convention Vasprun.homo/lumo use. Only meaningful if the
		// currently-loaded band window actually spans the occupied/unoccupied transition; returns -1
		// if every fetched band looks occupied (window too narrow/low to see the gap).
		[[nodiscard]] int FindHomoBandIndex(const std::vector<OrbitalRecord> &orbitals)
		{
			int homoIndex = -1;
			for (const OrbitalRecord &record : orbitals)
			{
				if ((record.up.occupation > 0.5f || record.down.occupation > 0.5f) && record.band > homoIndex)
					homoIndex = record.band;
			}
			return homoIndex;
		}

		// Occupation-based detection only works if the currently loaded band window happens to
		// straddle the occupied/unoccupied transition. If it doesn't (e.g. the whole loaded window
		// sits above or below the gap), fall back to whichever loaded band's energy is closest to
		// the known HOMO reference (bulk or this calc's own global gap) - keeps "Center on gap"
		// usable on the very first load instead of permanently disabled until the user gets lucky.
		[[nodiscard]] int FindGapAdjacentBandIndex(
			const std::vector<OrbitalRecord> &orbitals, const BandGapData *referenceGap)
		{
			const int occupiedHomo = FindHomoBandIndex(orbitals);
			if (occupiedHomo >= 0)
				return occupiedHomo;
			if (referenceGap == nullptr || orbitals.empty())
				return -1;

			int bestBand = -1;
			float bestDelta = std::numeric_limits<float>::max();
			for (const OrbitalRecord &record : orbitals)
			{
				const float delta = std::min(
					std::abs(record.up.energy - referenceGap->homo), std::abs(record.down.energy - referenceGap->homo));
				if (delta < bestDelta)
				{
					bestDelta = delta;
					bestBand = record.band;
				}
			}
			return bestBand;
		}
	} // namespace

	ElectronicStructurePanel::ElectronicStructurePanel(
		Ref<ElectronicStructureSession> session, Ref<EventBus> eventBus, std::string title, bool visibleByDefault)
		: IPanel(std::move(title), visibleByDefault), m_Session(std::move(session)), m_EventBus(std::move(eventBus))
	{
	}

	Ref<IPanel> ElectronicStructurePanel::Clone() const
	{
		return CreateRef<ElectronicStructurePanel>(*this);
	}

	void ElectronicStructurePanel::renderBandTable(
		ElectronicStructureSession::WindowState &state, const std::vector<OrbitalRecord> &filtered,
		RendererWindowState &windowState, float vbm)
	{
		if (filtered.empty())
		{
			ImGui::TextDisabled("No bands in the current window/threshold.");
			return;
		}

		constexpr ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY;
		if (!ImGui::BeginTable("bands", 7, flags, ImVec2(0.0f, 180.0f)))
			return;

		ImGui::TableSetupColumn("Band");
		ImGui::TableSetupColumn("E up");
		ImGui::TableSetupColumn("Occ up");
		ImGui::TableSetupColumn("E down");
		ImGui::TableSetupColumn("Occ down");
		ImGui::TableSetupColumn("Loc");
		ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 24.0f);
		ImGui::TableHeadersRow();

		int bandToRemove = -1;
		for (const OrbitalRecord &record : filtered)
		{
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			char label[32];
			std::snprintf(label, sizeof(label), "%d", record.band);
			if (ImGui::Selectable(
					label, state.selectedBand == record.band,
					ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap))
			{
				state.selectedBand = record.band;
				// Re-render whichever channels are already on for the new band; if neither is on
				// yet, default-enable spin up so selecting a band always shows *something*.
				if (!windowState.orbitalChannelUp.enabled && !windowState.orbitalChannelDown.enabled)
				{
					m_Session->EnsureChannelRendered(state, windowState, 0, 0);
				}
				else
				{
					if (windowState.orbitalChannelUp.enabled)
						m_Session->EnsureChannelRendered(state, windowState, 0, 0);
					if (windowState.orbitalChannelDown.enabled)
						m_Session->EnsureChannelRendered(state, windowState, 1, 1);
				}
			}
			ImGui::TableSetColumnIndex(1);
			ImGui::Text("%.3f", record.up.energy - vbm);
			ImGui::TableSetColumnIndex(2);
			ImGui::Text("%.2f", record.up.occupation);
			ImGui::TableSetColumnIndex(3);
			ImGui::Text("%.3f", record.down.energy - vbm);
			ImGui::TableSetColumnIndex(4);
			ImGui::Text("%.2f", record.down.occupation);
			ImGui::TableSetColumnIndex(5);
			ImGui::Text("%.2f", std::max(record.up.localization, record.down.localization));
			ImGui::TableSetColumnIndex(6);
			char deleteId[16];
			std::snprintf(deleteId, sizeof(deleteId), "x##del%d", record.band);
			if (ImGui::SmallButton(deleteId))
				bandToRemove = record.band;
		}
		ImGui::EndTable();

		if (ImGui::Button("Load all orbitals from table"))
			m_Session->PrefetchAllOrbitals(state, filtered);
		if (!state.pendingGridJobs.empty())
		{
			ImGui::SameLine();
			ImGui::TextDisabled("Loading... (%d left)", static_cast<int>(state.pendingGridJobs.size()));
		}

		// Removes just from the in-memory loaded set (not the CSV/calculation on disk) - lets you
		// prune bands you don't care about from a wide load without re-fetching a narrower range.
		if (bandToRemove >= 0 && state.data.has_value() && state.data->orbitals.has_value())
		{
			std::vector<OrbitalRecord> &orbitals = *state.data->orbitals;
			orbitals.erase(
				std::remove_if(
					orbitals.begin(), orbitals.end(),
					[bandToRemove](const OrbitalRecord &record) { return record.band == bandToRemove; }),
				orbitals.end());
			if (state.selectedBand == bandToRemove)
				state.selectedBand = -1;
		}
	}

	void ElectronicStructurePanel::renderBulkReferenceControls()
	{
		ImGui::Separator();
		ImGui::TextUnformatted("Bulk (pristine) reference for VBM/CBM");
		ImGui::TextDisabled(
			"A defect calculation's own global HOMO/LUMO can land on the mid-gap defect level "
			"itself, not the true band edges - point this at a pristine bulk calculation instead.");

		ImGui::TextDisabled("Shared across every open defect window - loaded once, not per window.");

		char pathBuffer[512];
		std::snprintf(pathBuffer, sizeof(pathBuffer), "%s", m_Session->BulkDirectory().String().c_str());
		ImGui::SetNextItemWidth(320.0f);
		Path bulkDirectory = m_Session->BulkDirectory();
		if (ImGui::InputText("##bulkdir", pathBuffer, sizeof(pathBuffer)))
			m_Session->SetBulkDirectory(Path(pathBuffer));
		ImGui::SameLine();
		if (ImGui::SmallButton("Browse..."))
		{
			Result<std::optional<Path>> picked = Platform::PickFolder(bulkDirectory);
			if (picked && picked->has_value())
				m_Session->SetBulkDirectory(picked->value());
		}

		ImGui::BeginDisabled(m_Session->BulkDirectory().Empty() || m_Session->IsBulkLoading());
		if (ImGui::Button("Reload bulk gap"))
			m_Session->DispatchBulkLoad();
		ImGui::EndDisabled();
		if (m_Session->IsBulkLoading())
		{
			ImGui::SameLine();
			ImGui::TextDisabled("Loading...");
		}
		if (m_Session->BulkGap().has_value())
		{
			ImGui::SameLine();
			ImGui::TextDisabled("Bulk VBM %.3f eV, CBM %.3f eV, Gap %.3f eV",
				m_Session->BulkGap()->homo, m_Session->BulkGap()->lumo, m_Session->BulkGap()->bandgap);
		}
		if (!m_Session->BulkError().empty())
			ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", m_Session->BulkError().c_str());
	}

	void ElectronicStructurePanel::renderWavefunctionControls(
		ElectronicStructureSession::WindowState &state, RendererWindowState &windowState)
	{
		ImGui::Separator();
		if (state.selectedBand < 0)
		{
			ImGui::TextDisabled("Select a band above to render its wavefunction.");
			return;
		}

		ImGui::Text("Selected band: %d", state.selectedBand);

		bool upEnabled = windowState.orbitalChannelUp.enabled;
		if (ImGui::Checkbox("Show spin up", &upEnabled))
		{
			windowState.orbitalChannelUp.enabled = upEnabled;
			if (upEnabled)
				m_Session->EnsureChannelRendered(state, windowState, 0, 0);
		}
		ImGui::SameLine();
		bool downEnabled = windowState.orbitalChannelDown.enabled;
		if (ImGui::Checkbox("Show spin down", &downEnabled))
		{
			windowState.orbitalChannelDown.enabled = downEnabled;
			if (downEnabled)
				m_Session->EnsureChannelRendered(state, windowState, 1, 1);
		}

		const auto isSlotLoading = [&](int slot)
		{
			return state.activeGridKeys[slot].has_value() && state.pendingGridJobs.contains(*state.activeGridKeys[slot]);
		};
		if (isSlotLoading(0) || isSlotLoading(1))
		{
			ImGui::SameLine();
			ImGui::TextDisabled("Loading...");
		}
		if (!state.gridError.empty())
			ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", state.gridError.c_str());

		ImGui::SetNextItemWidth(220.0f);
		// Continuous manipulation: cached grids are already GPU-uploaded, so every slider tick is
		// just a compute-shader re-dispatch per enabled channel, no Python re-fetch.
		if (ImGui::SliderFloat("Iso value", &state.isoValue, 0.001f, 5.0f, "%.3f", ImGuiSliderFlags_Logarithmic))
		{
			for (int slot = 0; slot < 2; ++slot)
			{
				if (!state.activeGridKeys[slot].has_value())
					continue;
				// Remember this value against the orbital(s) actually being shown, not just the
				// shared default - re-selecting this band later restores it via ResolveIsoValue.
				state.isoValueByKey[*state.activeGridKeys[slot]] = state.isoValue;
				const auto cached = state.gridCache.find(*state.activeGridKeys[slot]);
				if (cached != state.gridCache.end())
					m_Session->Layer().RegenerateOrbitalIsosurface(
						windowState.windowId, cached->second, state.isoValue, slot);
			}
		}

		// NoInputs: a compact clickable swatch (opens the full picker popup on click) instead of
		// permanently-expanded RGB drag fields - four of those side by side overflowed the panel
		// width, pushing "Negative" off-screen and out of reach entirely.
		constexpr ImGuiColorEditFlags kSwatchFlags = ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel;
		ImGui::Separator();
		ImGui::TextUnformatted("Spin up colors");
		ImGui::SameLine();
		ImGui::ColorEdit3("Positive##up", &windowState.orbitalChannelUp.positiveLobeColor.x, kSwatchFlags);
		ImGui::SameLine();
		ImGui::TextUnformatted("/");
		ImGui::SameLine();
		ImGui::ColorEdit3("Negative##up", &windowState.orbitalChannelUp.negativeLobeColor.x, kSwatchFlags);
		ImGui::SameLine();
		ImGui::SetNextItemWidth(140.0f);
		ImGui::SliderFloat("Opacity##up", &windowState.orbitalChannelUp.lobeAlpha, 0.05f, 1.0f);

		ImGui::TextUnformatted("Spin down colors");
		ImGui::SameLine();
		ImGui::ColorEdit3("Positive##down", &windowState.orbitalChannelDown.positiveLobeColor.x, kSwatchFlags);
		ImGui::SameLine();
		ImGui::TextUnformatted("/");
		ImGui::SameLine();
		ImGui::ColorEdit3("Negative##down", &windowState.orbitalChannelDown.negativeLobeColor.x, kSwatchFlags);
		ImGui::SameLine();
		ImGui::SetNextItemWidth(140.0f);
		ImGui::SliderFloat("Opacity##down", &windowState.orbitalChannelDown.lobeAlpha, 0.05f, 1.0f);

		ImGui::Separator();
		if (ImGui::Button("Export orbitals CSV"))
			m_Session->ExportOrbitalsCsv(state);
		if (!state.csvExportMessage.empty())
		{
			ImGui::SameLine();
			ImGui::TextDisabled("%s", state.csvExportMessage.c_str());
		}
	}

	void ElectronicStructurePanel::Render()
	{
		if (!IsVisible())
			return;

		ImGui::SetNextWindowSize(ImVec2(680.0f, 900.0f), ImGuiCond_FirstUseEver);
		bool stillOpen = true;
		if (!ImGui::Begin(GetTitle().c_str(), &stillOpen))
		{
			ImGui::End();
			return;
		}
		if (!stillOpen)
		{
			SetVisible(false);
			ImGui::End();
			return;
		}

		RendererWindowState *windowState = m_Session->FindFocusedWindow();
		if (windowState == nullptr || windowState->structure.sourcePath.Empty())
		{
			ImGui::TextDisabled("No structure window focused.");
			ImGui::End();
			return;
		}

		ElectronicStructureSession::WindowState &state = m_Session->Update(*windowState);

		ImGui::TextDisabled("%s", state.calculationDirectory.String().c_str());

		ImGui::SetNextItemWidth(160.0f);
		ImGui::InputInt("Band start (index)", &state.bandStart);
		ImGui::SetNextItemWidth(160.0f);
		ImGui::InputInt("Band end (index, inclusive)", &state.bandEnd);
		state.bandStart = std::max(0, state.bandStart);
		state.bandEnd = std::max(state.bandStart, state.bandEnd);

		const bool loadingOutput = state.pendingOutputJob != nullptr;
		ImGui::BeginDisabled(loadingOutput);
		if (ImGui::Button(state.data.has_value() ? "Reload" : "Load electronic structure"))
			m_Session->DispatchOutputLoad(state);
		ImGui::SameLine();
		// Re-centers the band window on the gap using the HOMO band index inferred from whatever is
		// currently loaded - occupation-based if the load spans the transition, otherwise nearest-
		// energy-to-reference-HOMO fallback (see FindGapAdjacentBandIndex). Reused below (after the
		// early-return guards) instead of recomputed, since bulkGap/data->gap don't change mid-frame.
		const BandGapData *referenceGap = m_Session->BulkGap().has_value()
			? &*m_Session->BulkGap()
			: (state.data.has_value() && state.data->gap.has_value() ? &*state.data->gap : nullptr);
		const int homoIndex = state.data.has_value() && state.data->orbitals.has_value()
			? FindGapAdjacentBandIndex(*state.data->orbitals, referenceGap)
			: -1;
		ImGui::SetNextItemWidth(80.0f);
		ImGui::InputInt("+/- orbitals##gapmargin", &state.gapWindowMargin);
		state.gapWindowMargin = std::clamp(state.gapWindowMargin, 1, 500);
		ImGui::SameLine();
		ImGui::BeginDisabled(homoIndex < 0 || loadingOutput);
		char centerLabel[48];
		std::snprintf(centerLabel, sizeof(centerLabel), "Center on gap (+/-%d)", state.gapWindowMargin);
		if (ImGui::Button(centerLabel))
		{
			state.bandStart = std::max(0, homoIndex - state.gapWindowMargin);
			state.bandEnd = homoIndex + state.gapWindowMargin;
			m_Session->DispatchOutputLoad(state);
		}
		ImGui::EndDisabled();
		ImGui::EndDisabled();
		if (loadingOutput)
		{
			ImGui::SameLine();
			ImGui::TextDisabled("Loading...");
		}
		if (!state.lastError.empty())
			ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", state.lastError.c_str());

		if (!state.data.has_value())
		{
			ImGui::End();
			return;
		}

		if (state.data->gap.has_value())
		{
			const BandGapData &gap = *state.data->gap;
			ImGui::Text("Calc HOMO/LUMO %.3f / %.3f eV   Gap %.3f eV", gap.homo, gap.lumo, gap.bandgap);
		}
		else
		{
			ImGui::TextDisabled("Calc band gap unavailable (no vasprun.xml)");
		}

		renderBulkReferenceControls();

		if (referenceGap != nullptr)
		{
			ImGui::Text("VBM(ref) %.3f eV   CBM(ref) %.3f eV   Gap(ref) %.3f eV",
				referenceGap->homo, referenceGap->lumo, referenceGap->bandgap);
		}
		else
		{
			ImGui::TextDisabled("No VBM/CBM reference available yet.");
		}

		if (!state.data->orbitals.has_value())
		{
			ImGui::TextDisabled("Orbitals unavailable (no WAVECAR)");
			ImGui::End();
			return;
		}

		ImGui::SetNextItemWidth(180.0f);
		ImGui::SliderFloat(
			"Localization threshold", &state.localizationThreshold, 0.0f, 500.0f, "%.2f", ImGuiSliderFlags_Logarithmic);

		const std::vector<OrbitalRecord> filtered = FilterByLocalizationThreshold(
			*state.data->orbitals, LocalizationThresholdSettings{state.localizationThreshold});
		const SpinMultiplicity classification = ClassifySpinMultiplicity(filtered);
		const char *classificationLabel = classification == SpinMultiplicity::Singlet ? "Singlet"
			: classification == SpinMultiplicity::Triplet                            ? "Triplet"
																					   : "Unknown";
		ImGui::Text("Classification (filtered window): %s", classificationLabel);

		const float vbm = (state.relativeToVbm && referenceGap != nullptr) ? referenceGap->homo : 0.0f;
		renderBandTable(state, filtered, *windowState, vbm);
		renderWavefunctionControls(state, *windowState);

		ImGui::End();
	}
} // namespace DefectStudio
