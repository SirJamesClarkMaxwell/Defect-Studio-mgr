#include "Core/dspch.hpp"

#include "Presentation/Panels/OccupationDiagramPanel.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <vector>

#include <imgui.h>
#include <implot.h>

namespace DefectStudio
{
	OccupationDiagramPanel::OccupationDiagramPanel(
		Ref<ElectronicStructureSession> session, std::string title, bool visibleByDefault)
		: IPanel(std::move(title), visibleByDefault), m_Session(std::move(session))
	{
	}

	Ref<IPanel> OccupationDiagramPanel::Clone() const
	{
		return CreateRef<OccupationDiagramPanel>(*this);
	}

	void OccupationDiagramPanel::renderPlot(
		ElectronicStructureSession::WindowState &state, RendererWindowState &windowState,
		const std::vector<OrbitalRecord> &filtered, float vbm, bool autofitRequested)
	{
		const BandGapData *referenceGap =
			m_Session->BulkGap().has_value() ? &*m_Session->BulkGap() : (state.data->gap.has_value() ? &*state.data->gap : nullptr);

		// Default view centers on the gap +/- 10% of the gap's own width, rather than a fixed eV
		// margin (which would swallow a small gap or barely matter for a huge one) or the full
		// min/max of every fetched band (a wide band window can include far-away core/continuum
		// states that would otherwise squash the gap region into a sliver). ImPlotCond_Once only
		// sets the *initial* view; the user can still zoom/pan freely afterwards, and the Autofit
		// button re-applies this same computation (see autofitRequested below).
		double yMin, yMax;
		if (referenceGap != nullptr)
		{
			const double gapWidth = static_cast<double>(referenceGap->lumo) - static_cast<double>(referenceGap->homo);
			const double margin = std::max(gapWidth * 0.10, 0.05);
			yMin = static_cast<double>(referenceGap->homo) - vbm - margin;
			yMax = static_cast<double>(referenceGap->lumo) - vbm + margin;
		}
		else
		{
			yMin = 1e9;
			yMax = -1e9;
			for (const OrbitalRecord &record : filtered)
			{
				yMin = std::min({yMin, static_cast<double>(record.up.energy) - vbm, static_cast<double>(record.down.energy) - vbm});
				yMax = std::max({yMax, static_cast<double>(record.up.energy) - vbm, static_cast<double>(record.down.energy) - vbm});
			}
			if (yMin > yMax)
			{
				yMin = 0.0;
				yMax = 1.0;
			}
		}

		// Fills whatever space the (resizable, dockable) window is given, instead of a fixed
		// height squeezed under a long list of controls.
		if (!ImPlot::BeginPlot("Occupation", ImGui::GetContentRegionAvail()))
			return;

		static const char *splitLabels[] = {"Up", "Down"};
		static const double mergedTickValue = 0.0;
		static const char *mergedLabels[] = {"Level"};
		ImPlot::SetupAxes("", state.relativeToVbm ? "Energy - VBM (eV)" : "Energy (eV)");
		if (state.splitSpinChannels)
		{
			ImPlot::SetupAxisTicks(ImAxis_X1, 0.0, 1.0, 2, splitLabels);
			ImPlot::SetupAxisLimits(ImAxis_X1, -0.5, 1.5, ImPlotCond_Always);
		}
		else
		{
			// The (v_min, v_max, n_ticks) overload requires v_min < v_max - a single-tick merged
			// axis needs the explicit-values overload instead (a degenerate (0.0, 0.0, 1, ...)
			// range hit an internal ImPlot assert here previously).
			ImPlot::SetupAxisTicks(ImAxis_X1, &mergedTickValue, 1, mergedLabels);
			ImPlot::SetupAxisLimits(ImAxis_X1, -0.5, 0.5, ImPlotCond_Always);
		}
		// Once = only the initial view (user can zoom/pan freely afterwards); the "Autofit" button
		// re-applies this same computed window on demand via Always, for just that one frame.
		ImPlot::SetupAxisLimits(ImAxis_Y1, yMin, yMax, autofitRequested ? ImPlotCond_Always : ImPlotCond_Once);
		ImPlot::SetupFinish();

		ImDrawList *drawList = ImPlot::GetPlotDrawList();
		const ImPlotRect limits = ImPlot::GetPlotLimits();
		// Raw ImDrawList calls (unlike ImPlot::PlotX) aren't auto-clipped to the plot area - a
		// level far outside the current Y view would otherwise draw its line/arrow anywhere on
		// screen the pixel math happens to land, overlapping unrelated UI above/below the plot.
		const ImVec2 plotPos = ImPlot::GetPlotPos();
		const ImVec2 plotSize = ImPlot::GetPlotSize();
		drawList->PushClipRect(plotPos, ImVec2(plotPos.x + plotSize.x, plotPos.y + plotSize.y), true);

		auto drawReferenceLine = [&](double energy, const char *label, ImU32 color)
		{
			const ImVec2 left = ImPlot::PlotToPixels(limits.X.Min, energy - static_cast<double>(vbm));
			const ImVec2 right = ImPlot::PlotToPixels(limits.X.Max, energy - static_cast<double>(vbm));
			drawList->AddLine(left, right, color, 1.5f);
			drawList->AddText(ImVec2(left.x + 4.0f, left.y + 2.0f), color, label);
		};
		if (referenceGap != nullptr)
		{
			drawReferenceLine(referenceGap->homo, "VBM", IM_COL32(120, 200, 255, 200));
			drawReferenceLine(referenceGap->lumo, "CBM", IM_COL32(255, 160, 120, 200));
		}

		constexpr float kLevelHalfWidthPx = 26.0f;
		constexpr float kArrowHalfHeightPx = 16.0f;
		constexpr float kArrowHeadPx = 6.0f;
		const ImU32 tickColor = IM_COL32(200, 200, 200, 255);
		const ImU32 upColor = IM_COL32(80, 140, 255, 255);
		const ImU32 downColor = IM_COL32(255, 115, 75, 255);

		auto drawArrow = [&](ImVec2 center, bool pointsUp, ImU32 color)
		{
			const float top = center.y - kArrowHalfHeightPx;
			const float bottom = center.y + kArrowHalfHeightPx;
			const float tipY = pointsUp ? top : bottom;
			const float tailY = pointsUp ? bottom : top;
			const float dir = pointsUp ? 1.0f : -1.0f;
			drawList->AddLine(ImVec2(center.x, tailY), ImVec2(center.x, tipY), color, 2.0f);
			drawList->AddTriangleFilled(
				ImVec2(center.x, tipY),
				ImVec2(center.x - kArrowHeadPx, tipY + dir * kArrowHeadPx),
				ImVec2(center.x + kArrowHeadPx, tipY + dir * kArrowHeadPx),
				color);
		};
		const ImU32 selectedTickColor = IM_COL32(255, 230, 90, 255);
		auto drawLevel = [&](ImVec2 center, bool selected)
		{
			// Selected = state.selectedBand, the same band a table row click (ElectronicStructurePanel)
			// or a diagram click (below) puts on screen in the 3D view - thicker + bright yellow so
			// it's obvious which level the isosurface currently belongs to.
			const float halfWidth = selected ? kLevelHalfWidthPx + 4.0f : kLevelHalfWidthPx;
			drawList->AddLine(
				ImVec2(center.x - halfWidth, center.y),
				ImVec2(center.x + halfWidth, center.y),
				selected ? selectedTickColor : tickColor, selected ? 3.0f : 1.5f);
		};
		// nr goes on the left of the tick, energy on the right - consistently, for every level.
		// `extraSpread` (the ladder offset below) only pushes the *labels* further from the tick;
		// the tick itself always stays kLevelHalfWidthPx (its true, honest position) so the diagram
		// doesn't lie about how close two levels actually are - only the text needs breathing room.
		auto drawLevelLabels =
			[&](ImVec2 center, float extraSpread, int band, float energy, const std::optional<std::string> &irrep)
		{
			const float labelOffset = kLevelHalfWidthPx + extraSpread;
			char nrBuffer[16];
			std::snprintf(nrBuffer, sizeof(nrBuffer), "#%d", band);
			const ImVec2 nrSize = ImGui::CalcTextSize(nrBuffer);
			drawList->AddText(
				ImVec2(center.x - labelOffset - 6.0f - nrSize.x, center.y - 7.0f), tickColor, nrBuffer);

			std::string energyBuffer(32, '\0');
			int written = std::snprintf(energyBuffer.data(), energyBuffer.size(), "%.3f eV", energy);
			energyBuffer.resize(written > 0 ? static_cast<std::size_t>(written) : 0);
			// Custom override shown alongside the automatic irrep, never replacing it (see
			// ElectronicStructurePanel::renderIrrepLabelEditor) - "b_1 (pi*)".
			if (irrep.has_value() && !irrep->empty())
			{
				energyBuffer += "  ";
				energyBuffer += *irrep;
				if (const std::string *override_ = m_Session->FindIrrepLabelOverride(irrep);
					override_ != nullptr && !override_->empty())
				{
					energyBuffer += " (";
					energyBuffer += *override_;
					energyBuffer += ")";
				}
			}
			drawList->AddText(ImVec2(center.x + labelOffset + 6.0f, center.y - 7.0f), tickColor, energyBuffer.c_str());
		};

		// Levels within 0.1 eV of their neighbor (sorted by energy) stack almost on top of each
		// other in pixel space and their labels become unreadable - each one in such a cluster gets
		// its LABEL progressively pushed further out (the ticks themselves never move/resize, only
		// the text spreads into a ladder). Computed once per column (up/down are independent
		// columns in split mode; merged mode has one column keyed on up.energy, matching what's
		// actually drawn there).
		constexpr float kCloseLevelThresholdEv = 0.1f;
		constexpr float kLadderStepPx = 16.0f;
		const auto computeLadderOffsets = [&](bool useDownEnergy)
		{
			std::vector<std::size_t> order(filtered.size());
			for (std::size_t i = 0; i < order.size(); ++i)
				order[i] = i;
			const auto energyOf = [&](std::size_t i)
			{
				return (useDownEnergy ? filtered[i].down.energy : filtered[i].up.energy) - vbm;
			};
			std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) { return energyOf(a) < energyOf(b); });

			std::vector<float> offsetPx(filtered.size(), 0.0f);
			int stagger = 0;
			for (std::size_t i = 1; i < order.size(); ++i)
			{
				const float delta = energyOf(order[i]) - energyOf(order[i - 1]);
				stagger = (delta < kCloseLevelThresholdEv) ? stagger + 1 : 0;
				offsetPx[order[i]] = static_cast<float>(stagger) * kLadderStepPx;
			}
			return offsetPx;
		};
		const std::vector<float> upLadder = computeLadderOffsets(false);
		const std::vector<float> downLadder = state.splitSpinChannels ? computeLadderOffsets(true) : upLadder;

		// Click-to-select: same dispatch ElectronicStructurePanel's band-table row click uses (see
		// its renderBandTable) - reused here verbatim rather than extracted, since it's an 11-line
		// wrapper around EnsureChannelRendered either way. Hit-tested in pixel space against the
		// exact same centers this loop already computes to draw each tick - no separate/duplicate
		// geometry, and correct by construction for the ladder-spread label offsets (those only move
		// the label, never the tick, so pixel hit-test on the tick position stays accurate).
		const bool clickedThisFrame = ImPlot::IsPlotHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
		const ImVec2 mousePos = ImGui::GetMousePos();
		int clickedBand = -1;
		float bestDistSq = std::numeric_limits<float>::max();
		constexpr float kHitHalfWidthPx = kLevelHalfWidthPx + 4.0f;
		constexpr float kHitHalfHeightPx = 8.0f;
		const auto testHit = [&](ImVec2 center, int band)
		{
			if (!clickedThisFrame)
				return;
			if (std::abs(mousePos.x - center.x) > kHitHalfWidthPx || std::abs(mousePos.y - center.y) > kHitHalfHeightPx)
				return;
			const float dx = mousePos.x - center.x;
			const float dy = mousePos.y - center.y;
			const float distSq = dx * dx + dy * dy;
			if (distSq < bestDistSq)
			{
				bestDistSq = distSq;
				clickedBand = band;
			}
		};

		for (std::size_t index = 0; index < filtered.size(); ++index)
		{
			const OrbitalRecord &record = filtered[index];
			const bool isSelected = record.band == state.selectedBand;

			if (state.splitSpinChannels)
			{
				const ImVec2 upCenter = ImPlot::PlotToPixels(0.0, static_cast<double>(record.up.energy) - vbm);
				drawLevel(upCenter, isSelected);
				drawLevelLabels(upCenter, upLadder[index], record.band, record.up.energy - vbm, record.up.irrep);
				if (record.up.occupation > 0.5f)
					drawArrow(upCenter, true, upColor);
				testHit(upCenter, record.band);

				const ImVec2 downCenter = ImPlot::PlotToPixels(1.0, static_cast<double>(record.down.energy) - vbm);
				drawLevel(downCenter, isSelected);
				drawLevelLabels(downCenter, downLadder[index], record.band, record.down.energy - vbm, record.down.irrep);
				if (record.down.occupation > 0.5f)
					drawArrow(downCenter, false, downColor);
				testHit(downCenter, record.band);
			}
			else
			{
				// Merged view: closed-shell levels have up==down energy, so this reads as one
				// level with both spin arrows overlaid slightly apart, not two separate columns.
				const ImVec2 center = ImPlot::PlotToPixels(0.0, static_cast<double>(record.up.energy) - vbm);
				drawLevel(center, isSelected);
				drawLevelLabels(center, upLadder[index], record.band, record.up.energy - vbm, record.up.irrep);
				if (record.up.occupation > 0.5f)
					drawArrow(ImVec2(center.x - 6.0f, center.y), true, upColor);
				if (record.down.occupation > 0.5f)
					drawArrow(ImVec2(center.x + 6.0f, center.y), false, downColor);
				testHit(center, record.band);
			}
		}

		drawList->PopClipRect();
		ImPlot::EndPlot();

		if (clickedBand >= 0)
		{
			state.selectedBand = clickedBand;
			// Re-render whichever channels are already on for the new band; if neither is on yet,
			// default-enable spin up so clicking a level always shows *something* - identical to
			// ElectronicStructurePanel::renderBandTable's row-click behavior.
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
	}

	void OccupationDiagramPanel::Render()
	{
		if (!IsVisible())
			return;

		ImGui::SetNextWindowSize(ImVec2(900.0f, 600.0f), ImGuiCond_FirstUseEver);
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
		if (!state.data.has_value() || !state.data->orbitals.has_value())
		{
			ImGui::TextDisabled("No orbital data loaded yet - open Electronic Structure to load some.");
			ImGui::End();
			return;
		}

		ImGui::Checkbox("Split spin channels", &state.splitSpinChannels);
		ImGui::SameLine();
		ImGui::Checkbox("Energy relative to VBM", &state.relativeToVbm);
		ImGui::SameLine();
		const bool autofitRequested = ImGui::Button("Autofit");

		const BandGapData *referenceGap =
			m_Session->BulkGap().has_value() ? &*m_Session->BulkGap() : (state.data->gap.has_value() ? &*state.data->gap : nullptr);
		const float vbm = (state.relativeToVbm && referenceGap != nullptr) ? referenceGap->homo : 0.0f;

		const std::vector<OrbitalRecord> filtered = FilterByLocalizationThreshold(
			*state.data->orbitals, LocalizationThresholdSettings{state.localizationThreshold});

		renderPlot(state, *windowState, filtered, vbm, autofitRequested);

		ImGui::End();
	}
} // namespace DefectStudio
