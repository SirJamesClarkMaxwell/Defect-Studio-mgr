#include "Core/dspch.hpp"

#include "Presentation/Panels/SceneDensityEditor.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include <imgui.h>

#include "Core/Logging/Logger.hpp"
#include "Core/Platform/FileDialog.hpp"
#include "Presentation/Panels/PropertyGrid.hpp"
#include "Renderer/RendererLayer.hpp"
#include "Renderer/Scene/SceneSelection.hpp"

namespace DefectStudio
{
	namespace
	{
		using SceneDensity = RendererWindowState::SceneDensity;

		// One activation of a held widget is one undo entry - same shape as the orbital editor.
		template <typename T, typename WidgetFn>
		bool DrawUndoableValue(RendererWindowState &windowState, T &modelValue, WidgetFn &&widget)
		{
			T edited = modelValue;
			const bool changed = widget(edited);
			if (ImGui::IsItemActivated())
				PushPinnedMeasurementUndoSnapshot(windowState);
			if (changed)
				modelValue = edited;
			return changed;
		}

		[[nodiscard]] std::optional<Path> PickChgcar(const Path &current)
		{
			const Path directory = current.Empty() ? Path() : current.parent_path();
			Result<std::optional<Path>> picked = Platform::PickOpenFile(directory, "", "");
			if (!picked)
			{
				DS_LOG_WARN("CHGCAR file dialog failed: {}", picked.Error().technicalDetails);
				return std::nullopt;
			}
			return *picked;
		}

		[[nodiscard]] std::string ShortPath(const Path &path)
		{
			if (path.Empty())
				return "brak";
			const Path parent = path.parent_path().filename();
			return parent.Empty() ? path.filename().Utf8() : parent.Utf8() + "/" + path.filename().Utf8();
		}

		// Read-only path field plus "..." (pick another) and, for the optional reference, "x" (clear).
		// Returns true when the path changed; the caller pushes the undo entry first.
		bool DrawPathField(RendererWindowState &windowState, const char *id, Path &path, const bool clearable)
		{
			ImGui::PushID(id);
			const ImGuiStyle &style = ImGui::GetStyle();
			const float buttonWidth = ImGui::GetFrameHeight();
			const float buttons = clearable ? 2.0f : 1.0f;
			std::string shown = ShortPath(path);
			ImGui::SetNextItemWidth(-(buttonWidth + style.ItemInnerSpacing.x) * buttons);
			ImGui::InputText("##path", shown.data(), shown.size() + 1, ImGuiInputTextFlags_ReadOnly);
			if (!path.Empty())
				ImGui::SetItemTooltip("%s", path.Utf8().c_str());
			bool changed = false;
			ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
			if (ImGui::Button("...", ImVec2(buttonWidth, 0.0f)))
			{
				if (const std::optional<Path> picked = PickChgcar(path); picked.has_value() && *picked != path)
				{
					PushPinnedMeasurementUndoSnapshot(windowState);
					path = *picked;
					changed = true;
				}
			}
			ImGui::SetItemTooltip("Wybierz plik CHGCAR");
			if (clearable)
			{
				ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
				ImGui::BeginDisabled(path.Empty());
				if (ImGui::Button("x", ImVec2(buttonWidth, 0.0f)))
				{
					PushPinnedMeasurementUndoSnapshot(windowState);
					path = Path();
					changed = true;
				}
				ImGui::EndDisabled();
				ImGui::SetItemTooltip("Bez odejmowania");
			}
			ImGui::PopID();
			return changed;
		}

		void DrawStatus(const SceneDensity &density)
		{
			switch (density.loadState)
			{
			case SceneDensity::LoadState::Pending:
			case SceneDensity::LoadState::Loading:
				ImGui::AlignTextToFramePadding();
				ImGui::TextDisabled("Wczytywanie CHGCAR...");
				return;
			case SceneDensity::LoadState::Failed:
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.40f, 0.35f, 1.0f));
				ImGui::TextWrapped("%s", density.loadError.empty() ? "Błąd wczytywania." : density.loadError.c_str());
				ImGui::PopStyleColor();
				return;
			case SceneDensity::LoadState::Ready:
				ImGui::AlignTextToFramePadding();
				ImGui::TextUnformatted("Wczytano");
				return;
			}
		}

		// Whole-cell numbers. The integral of the spin density is the cell's moment, so it is
		// labelled in mu_B rather than electrons.
		void DrawStatistics(const RendererWindowState &windowState, const SceneDensity &density, const float labelWidth)
		{
			if (density.data == nullptr)
				return;
			const DensityGridStatistics &stats = density.data->statistics;
			const glm::ivec3 &dims = density.data->grid.dimensions;
			const bool moment = density.component == DensityComponent::Magnetization;
			char text[96];
			PropertyGrid grid("##DensityStatistics", labelWidth);
			if (!grid)
				return;
			std::snprintf(text, sizeof(text), "%d x %d x %d", dims.x, dims.y, dims.z);
			grid.Value("Siatka", text);
			std::snprintf(text, sizeof(text), "%.4f %s", stats.integral, moment ? "μB" : "e");
			grid.Value("Całka", text, moment ? "Moment magnetyczny komórki" : "Liczba elektronów w komórce");
			std::snprintf(text, sizeof(text), "%.4f %s", stats.absIntegral, moment ? "μB" : "e");
			grid.Value("Całka |.|", text,
				"Większa od |całki|, gdy są obszary przeciwnego znaku (np. polaryzacja spinowa sąsiadów).");
			std::snprintf(text, sizeof(text), "%.4g ... %.4g e/Å³", stats.minimum, stats.maximum);
			grid.Value("Zakres", text);

			const std::size_t atoms = windowState.structure.atoms.size();
			std::snprintf(text, sizeof(text), "%d", stats.atomCount);
			grid.Value("Atomy w pliku", text);
			if (atoms != static_cast<std::size_t>(stats.atomCount))
			{
				grid.Row("");
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.75f, 0.30f, 1.0f));
				ImGui::TextWrapped("Struktura w oknie ma %zu atomów - gęstość może nie pasować do tej struktury.", atoms);
				ImGui::PopStyleColor();
			}
			const glm::mat3 &cell = density.data->grid.cell;
			const glm::mat3 &lattice = windowState.structure.lattice;
			float cellMismatch = 0.0f;
			for (int axis = 0; axis < 3; ++axis)
				cellMismatch = std::max(cellMismatch, glm::length(cell[axis] - lattice[axis]));
			if (cellMismatch > 1e-3f)
			{
				grid.Row("");
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.75f, 0.30f, 1.0f));
				ImGui::TextWrapped("Komórka CHGCAR różni się od komórki struktury (do %.3f Å).", cellMismatch);
				ImGui::PopStyleColor();
			}
		}
	} // namespace

	SceneDensity MakeSceneDensity(const Path &chgcarPath, const DensityComponent component)
	{
		SceneDensity density;
		density.chgcarPath = chgcarPath;
		density.component = component;
		density.displayName = ShortPath(chgcarPath);
		return density;
	}

	float DefaultDensityIsoValue(const DensityGridStatistics &statistics)
	{
		return 0.1f * std::max(std::abs(statistics.minimum), std::abs(statistics.maximum));
	}

	void InvalidateSceneDensity(SceneDensity &density)
	{
		density.data.reset();
		density.loadState = SceneDensity::LoadState::Pending;
		density.loadError.clear();
		density.isoValue = 0.0f;
	}

	void AddSceneDensityFromFileDialog(RendererWindowState &windowState)
	{
		const std::optional<Path> picked = PickChgcar(Path());
		if (!picked.has_value())
			return;
		PushPinnedMeasurementUndoSnapshot(windowState);
		SceneDensity density = MakeSceneDensity(*picked);
		density.id = windowState.sceneRegistry.AllocateObjectId();
		const SceneObjectId id = density.id;
		windowState.sceneDensities.push_back(std::move(density));
		ClearAllSceneSelection(windowState);
		windowState.selectedSceneDensities = {id};
	}

	void EraseSceneDensities(RendererWindowState &windowState, const std::vector<SceneObjectId> &ids)
	{
		std::erase_if(windowState.sceneDensities, [&ids](const SceneDensity &density) {
			return std::find(ids.begin(), ids.end(), density.id) != ids.end();
		});
		windowState.selectedSceneDensities.clear();
	}

	void DuplicateSelectedSceneDensities(RendererWindowState &windowState)
	{
		if (windowState.selectedSceneDensities.empty())
			return;
		PushPinnedMeasurementUndoSnapshot(windowState);
		std::vector<SceneObjectId> copies;
		for (const SceneObjectId id : windowState.selectedSceneDensities)
		{
			const SceneDensity *source = FindAnnotation(windowState.sceneDensities, id);
			if (source == nullptr)
				continue;
			SceneDensity copy = *source;
			copy.id = windowState.sceneRegistry.AllocateObjectId();
			copy.persistKey.clear();
			copy.displayName += " (kopia)";
			copies.push_back(copy.id);
			windowState.sceneDensities.push_back(std::move(copy));
		}
		windowState.selectedSceneDensities = std::move(copies);
	}

	void DrawSelectedSceneDensitySection(RendererWindowState &windowState)
	{
		if (windowState.selectedSceneDensities.empty())
			return;
		SceneDensity *density = FindAnnotation(windowState.sceneDensities, windowState.selectedSceneDensities.front());
		if (density == nullptr)
			return;

		ImGui::PushID("##SceneDensity");
		ImGui::SeparatorText("Gęstość z CHGCAR");
		if (windowState.selectedSceneDensities.size() > 1)
			ImGui::TextDisabled("Zaznaczono %zu - edytujesz pierwszą.", windowState.selectedSceneDensities.size());

		const float labelWidth = PropertyGridLabelWidth(
			{"Nazwa", "Plik", "Składowa", "Odejmij", "Stan", "Izowartość", "Ujemna faza", "Kolor +", "Kolor -",
				"Krycie", "Atomy w pliku", "Całka |.|"});
		constexpr ImGuiTreeNodeFlags kOpen = ImGuiTreeNodeFlags_DefaultOpen;

		if (ImGui::CollapsingHeader("Źródło", kOpen))
		{
			PropertyGrid grid("##DensitySource", labelWidth);
			if (grid)
			{
				grid.Row("Nazwa");
				char name[128];
				std::snprintf(name, sizeof(name), "%s", density->displayName.c_str());
				if (ImGui::InputText("##name", name, sizeof(name)))
					density->displayName = name;
				if (ImGui::IsItemActivated())
					PushPinnedMeasurementUndoSnapshot(windowState);

				grid.Row("Plik", "CHGCAR obliczenia, którego gęstość jest rysowana.");
				if (DrawPathField(windowState, "chgcar", density->chgcarPath, false))
					InvalidateSceneDensity(*density);

				grid.Row("Składowa");
				if (ImGui::BeginCombo("##component", DensityComponentDisplayName(density->component)))
				{
					for (const DensityComponent component : kDensityComponents)
					{
						const bool selected = component == density->component;
						if (ImGui::Selectable(DensityComponentDisplayName(component), selected) && !selected)
						{
							PushPinnedMeasurementUndoSnapshot(windowState);
							density->component = component;
							InvalidateSceneDensity(*density);
						}
						if (selected)
							ImGui::SetItemDefaultFocus();
					}
					ImGui::EndCombo();
				}

				grid.Row("Odejmij",
					"Ta sama składowa z innego CHGCAR na tej samej siatce jest odejmowana - np. "
					"ρ(q) − ρ(0) pokazuje, gdzie trafia dodany ładunek.");
				if (DrawPathField(windowState, "reference", density->referencePath, true))
					InvalidateSceneDensity(*density);

				grid.Row("Stan");
				DrawStatus(*density);
			}
		}

		if (ImGui::CollapsingHeader("Izopowierzchnia", kOpen))
		{
			PropertyGrid grid("##DensitySurface", labelWidth);
			if (grid)
			{
				const DensityGridStatistics *stats = density->data ? &density->data->statistics : nullptr;
				const float peak = stats ? std::max(std::abs(stats->minimum), std::abs(stats->maximum)) : 0.0f;
				grid.Row("Izowartość", "Powierzchnia rysowana przy +izowartości i −izowartości. Ctrl+klik: wpisz wartość.");
				ImGui::BeginDisabled(peak <= 0.0f);
				DrawUndoableValue(windowState, density->isoValue, [peak](float &value) {
					return ImGui::SliderFloat("##iso", &value, peak * 1e-4f, peak, "%.4g e/Å³",
						ImGuiSliderFlags_Logarithmic);
				});
				ImGui::EndDisabled();
				if (peak > 0.0f)
					ImGui::SetItemTooltip("%.1f %% wartości szczytowej (%.4g e/Å³)", 100.0f * density->isoValue / peak, peak);

				grid.Row("Ujemna faza", "Rysuj też powierzchnię −izowartości (spin ↓ / ubytek ładunku).");
				DrawUndoableValue(windowState, density->showNegative, [](bool &value) {
					return ImGui::Checkbox("##negative", &value);
				});
				grid.Row("Kolor +");
				DrawUndoableValue(windowState, density->positiveColor, [](glm::vec3 &value) {
					return ImGui::ColorEdit3("##positive", &value.x, ImGuiColorEditFlags_NoInputs);
				});
				grid.Row("Kolor -");
				DrawUndoableValue(windowState, density->negativeColor, [](glm::vec3 &value) {
					return ImGui::ColorEdit3("##negativeColor", &value.x, ImGuiColorEditFlags_NoInputs);
				});
				grid.Row("Krycie");
				DrawUndoableValue(windowState, density->alpha, [](float &value) {
					return ImGui::SliderFloat("##alpha", &value, 0.05f, 1.0f, "%.2f");
				});
			}
		}

		if (density->data != nullptr && ImGui::CollapsingHeader("Statystyki", kOpen))
			DrawStatistics(windowState, *density, labelWidth);
		ImGui::PopID();
	}
} // namespace DefectStudio
