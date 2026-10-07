#include "Core/dspch.hpp"

#include "Presentation/Panels/SceneDensityLocalisation.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

#include <imgui.h>
#include <implot.h>

#include "Presentation/Panels/PropertyGrid.hpp"
#include "Renderer/RendererLayer.hpp"
#include "Renderer/Scene/SceneDensityAnalysis.hpp"

namespace DefectStudio
{
	void DrawSceneDensityLocalisation(RendererWindowState &windowState,
		RendererWindowState::SceneDensity &density, const float labelWidth)
	{
		if (!density.data)
			return;
		{
			PropertyGrid grid("##DensityLocalisation", labelWidth);
			if (grid)
			{
				grid.Row("Środek");
				if (ImGui::BeginCombo("##centreMode", density.analysisCentre ? "Ręczny" : "Centroid |ρ|"))
				{
					if (ImGui::Selectable("Centroid |ρ|", !density.analysisCentre) && density.analysisCentre)
					{
						PushPinnedMeasurementUndoSnapshot(windowState);
						density.analysisCentre.reset();
					}
					if (ImGui::Selectable("Ręczny", density.analysisCentre.has_value()) && !density.analysisCentre)
					{
						PushPinnedMeasurementUndoSnapshot(windowState);
						density.analysisCentre = SceneDensityAnalysisCentre(density).value_or(glm::vec3(0.0f));
					}
					ImGui::EndCombo();
				}

				grid.Row("Pozycja");
				glm::vec3 centre = SceneDensityAnalysisCentre(density).value_or(glm::vec3(0.0f));
				const bool centreChanged = ImGui::DragFloat3("##analysisCentre", &centre.x, 0.05f, 0.0f, 0.0f, "%.3f Å");
				if (ImGui::IsItemActivated())
					PushPinnedMeasurementUndoSnapshot(windowState);
				if (centreChanged && std::isfinite(centre.x) && std::isfinite(centre.y) && std::isfinite(centre.z))
					density.analysisCentre = centre;

				grid.Row("--center", "Współrzędne ułamkowe dla sxdefectalign --relative --center");
				const auto fractional = SceneDensityAnalysisCentreFractional(density);
				const std::string formatted = fractional ? FormatSxdefectalignCentre(*fractional) : "—";
				ImGui::AlignTextToFramePadding();
				ImGui::TextUnformatted(formatted.c_str());
				ImGui::SetItemTooltip("Współrzędne ułamkowe dla sxdefectalign --relative --center");
				ImGui::SameLine();
				ImGui::BeginDisabled(!fractional);
				if (ImGui::SmallButton("Kopiuj"))
					ImGui::SetClipboardText(formatted.c_str());
				ImGui::EndDisabled();

				grid.Row("");
				const auto selectedCentre = SelectedAtomsCentroid(windowState);
				ImGui::BeginDisabled(!selectedCentre);
				if (ImGui::Button("Z zaznaczonych atomów"))
				{
					PushPinnedMeasurementUndoSnapshot(windowState);
					density.analysisCentre = selectedCentre;
				}
				ImGui::EndDisabled();
				ImGui::SameLine();
				if (ImGui::Button("Centroid"))
				{
					PushPinnedMeasurementUndoSnapshot(windowState);
					density.analysisCentre.reset();
				}

				grid.Row("Promień R");
				float radius = density.analysisRadius;
				const bool radiusChanged = ImGui::DragFloat("##analysisRadius", &radius, 0.05f, 0.5f, 20.0f, "%.2f Å");
				if (ImGui::IsItemActivated())
					PushPinnedMeasurementUndoSnapshot(windowState);
				if (radiusChanged && std::isfinite(radius))
					density.analysisRadius = std::clamp(radius, 0.5f, 20.0f);

				const auto profile = SceneDensityRadialProfile(density);
				if (profile)
				{
					char text[96];
					std::snprintf(text, sizeof(text), "%.1f %% |ρ|", 100.0f * profile->FractionWithin(density.analysisRadius));
					grid.Value("W kuli R", text);
					std::snprintf(text, sizeof(text), "%.2f Å / %.2f Å", profile->RadiusHolding(0.5f), profile->RadiusHolding(0.9f));
					grid.Value("R50 / R90", text);
				}
				else
					grid.Value("W kuli R", "Brak centroidu |ρ|.");
			}
		}

		const auto profile = SceneDensityRadialProfile(density);
		if (!profile)
			return;
		if (profile->FractionWithin(density.analysisRadius) < kLocalisedFractionWarning)
		{
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.75f, 0.30f, 1.0f));
			ImGui::TextWrapped("Gęstość zdelokalizowana - poprawka FNV dla tego stanu nie ma sensu.");
			ImGui::PopStyleColor();
		}
		const double maxRadius = std::max(2.0 * density.analysisRadius, 1.5 * profile->RadiusHolding(0.9f));
		std::array<double, 200> radii, fractions;
		for (std::size_t i = 0; i < radii.size(); ++i)
		{
			radii[i] = maxRadius * static_cast<double>(i) / static_cast<double>(radii.size() - 1);
			fractions[i] = profile->FractionWithin(static_cast<float>(radii[i]));
		}
		if (ImPlot::BeginPlot("##DensityCumulative", ImVec2(-1.0f, 140.0f), ImPlotFlags_NoLegend))
		{
			ImPlot::SetupAxes("r (Å)", "Ułamek |ρ|");
			ImPlot::SetupAxesLimits(0.0, maxRadius, 0.0, 1.0, ImPlotCond_Always);
			ImPlot::PlotLine("##Cumulative", radii.data(), fractions.data(), static_cast<int>(radii.size()));
			double radius = density.analysisRadius;
			bool clicked = false;
			const bool changed = ImPlot::DragLineX(0, &radius, ImVec4(1.0f, 1.0f, 1.0f, 1.0f),
				1.5f, ImPlotDragToolFlags_NoFit, &clicked);
			if (ImGui::IsItemActivated() || clicked)
				PushPinnedMeasurementUndoSnapshot(windowState);
			if (changed && std::isfinite(radius))
				density.analysisRadius = static_cast<float>(std::clamp(radius, 0.5, 20.0));
			ImPlot::EndPlot();
		}
	}
} // namespace DefectStudio
