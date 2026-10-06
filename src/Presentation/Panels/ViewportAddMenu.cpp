#include "Core/dspch.hpp"
#include "Presentation/Panels/ViewportAddMenu.hpp"
#include "Presentation/Panels/SceneOrientationControls.hpp"

#include <algorithm>
#include <optional>
#include <string_view>
#include <imgui.h>
#include "Core/Commands/CommandRegistry.hpp"
#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Events/RendererEvents.hpp"
#include "Core/Logging/Logger.hpp"
#include "Presentation/Panels/RendererPanelOrbitalMenu.hpp"
#include "Presentation/Operators/SceneOperatorRegistry.hpp"
#include "Presentation/Panels/OperatorRedoPanel.hpp"
#include "Presentation/Panels/ScenePathDevMenu.hpp"
#include "Presentation/Panels/ScenePathOperations.hpp"
#include "Presentation/Panels/ViewportVacancyAdd.hpp"
#include "Presentation/Panels/ViewportDefectFrame.hpp"
#include "Presentation/Panels/ViewportTextEditor.hpp"
#include "Renderer/RendererLayer.hpp"
#include "Renderer/Scene/SceneFreeLabelAnchors.hpp"
#include "Renderer/Scene/SceneSystem.hpp"

namespace DefectStudio
{
	namespace
	{
		std::optional<glm::vec3> SelectedAtomCentroid(const RendererWindowState &windowState)
		{
			glm::vec3 sum(0.0f);
			std::size_t count = 0;
			for (const auto index : windowState.selectedAtomIndices)
				if (index < windowState.structure.atoms.size())
				{
					sum += windowState.structure.atoms[index].cartesianPosition;
					++count;
				}
			return count == 0 ? std::nullopt : std::optional<glm::vec3>(sum / static_cast<float>(count));
		}
	}
	void DrawDefectAddItems(RendererWindowState &windowState, CommandRegistry *registry, const glm::vec3 &position, float bondRadiusMultiplier)
	{
		const bool editable = registry != nullptr && !windowState.structure.domainStructureId.empty();
		const std::optional<glm::vec3> centroid = SelectedAtomCentroid(windowState);
		if (ImGui::MenuItem("Wakans (Vacancy)", nullptr, false, editable))
			AddVacancyAt(windowState, registry, centroid.value_or(position));
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
			ImGui::SetTooltip("Wakans w środku zaznaczonych atomów, a bez zaznaczenia w miejscu kliknięcia / kursora 3D.");

		const bool canBond = !windowState.structure.vacancies.empty();
		if (ImGui::MenuItem("Wiązania wakansów", nullptr, false, canBond))
			(void)AddVacancyBonds(windowState, bondRadiusMultiplier);
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
			ImGui::SetTooltip("Zaznaczone atomy -> wakans (zaznaczony albo najbliższy),\n"
							  "albo zaznaczone wakanse (bez zaznaczenia: wszystkie) -> pierwsza sfera sąsiadów.\n"
							  "Dwukolorowe linie atom -> wakans, styl każdej osobno.");

		if (ImGui::MenuItem("Etykiety wakansów", nullptr, false, !windowState.structure.vacancies.empty()))
		{
			std::vector<std::size_t> vacancies;
			for (std::size_t index = 0; index < windowState.structure.vacancies.size(); ++index)
			{
				if (!windowState.selectedVacancies.empty() &&
					std::find(windowState.selectedVacancies.begin(), windowState.selectedVacancies.end(), index) == windowState.selectedVacancies.end())
					continue;
				if (std::none_of(windowState.freeLabels.begin(), windowState.freeLabels.end(), [index](const auto &label) {
						return label.anchorVacancy == index;
					}))
					vacancies.push_back(index);
			}
			if (!vacancies.empty())
			{
				PushPinnedMeasurementUndoSnapshot(windowState);
				windowState.selectedFreeLabels = AddVacancyLabels(windowState, vacancies);
				windowState.selectedPinnedMeasurements.clear();
				windowState.selectedSceneOrbitals.clear();
				windowState.selectedScenePlanes.clear();
				windowState.selectedScenePaths.clear();
				windowState.selectedVacancies.clear();
				windowState.defectFrameSelected = false;
				SceneSystem::ClearStructureSelection(windowState.sceneRegistry, windowState);
				SceneSystem::SyncLabelSelection(windowState.sceneRegistry, windowState);
			}
		}

		if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
			ImGui::SetTooltip("Dodaj wakans. Zaznaczone wakanse otrzymają etykiety; bez zaznaczenia: wszystkie.");
		DrawDefectFrameAddMenu(windowState, registry, position);
	}

	namespace
	{
		void DrawSceneDrawingAddItems(RendererWindowState &windowState, const glm::vec3 &position,
			const auto &addCurvedArrow)
		{
			DrawSegmentAddItems(windowState);
			std::size_t count = 0;
			for (const auto index : windowState.selectedAtomIndices)
				count += index < windowState.structure.atoms.size() ? 1u : 0u;
			const bool atomArrows = count >= 2;
			if (count < 2)
				for (const auto index : windowState.selectedVacancies)
					count += index < windowState.structure.vacancies.size() ? 1u : 0u;
			if (ImGui::MenuItem("Zakrzywiona strzałka (C_n)", nullptr, false, atomArrows || count == 2))
				addCurvedArrow();
			if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
				ImGui::SetTooltip("Zaznacz co najmniej dwa atomy. Dwa atomy: dwie strzałki wokół wiązania; trzy lub więcej atomów: zamknięty cykl w dodatnim kierunku obrotu.\n"
					"Można też zaznaczyć dwa końce będące atomami lub wakansami.\n"
					"Oś: z układu defektu; inaczej przez zaznaczony wakans lub środek atomów, prostopadle do ich płaszczyzny (dla dwóch końców: płaszczyzny sąsiadów).");
			DrawFreeSegmentAddItems(windowState, position);
			if (ImGui::BeginMenu("Krzywa"))
			{
				const auto add = [&](ScenePathDevPreset preset, const std::string &label) {
					ScenePath path = MakeDevScenePath(preset, position);
					path.name = label;
					path.style.endDecoration.kind = PathDecorationKind::None;
					const auto result = preset == ScenePathDevPreset::Line ?
						AddFreeScenePathSegment(windowState, position, false) :
						AddScenePath(MakeWindowPathEditContext(windowState), std::move(path));
					if (result)
						SelectAddedScenePaths(windowState, {result.Value()});
					else
						DS_LOG_WARN("Add curve failed: {}", result.Error().technicalDetails);
				};
				if (ImGui::MenuItem("Prosta")) add(ScenePathDevPreset::Line, "Prosta");
				if (ImGui::MenuItem("Krzywa Béziera")) add(ScenePathDevPreset::Cubic, "Krzywa Béziera");
				if (ImGui::MenuItem("Łuk")) add(ScenePathDevPreset::Arc, "Łuk");
				ImGui::EndMenu();
			}

		}
		void DrawScenePlaneAddItems(RendererWindowState &windowState, const glm::vec3 &position)
		{
			DrawPlaneAddItem(windowState);
			DrawFreePlaneAddItem(windowState, position);
			if (windowState.structure.defectFrame) DrawScenePlaneDefectPlacementMenu(windowState, true);
		}
	}
	void DrawSceneAddMenu(RendererWindowState &windowState, const WeakRef<CommandRegistry> &commands,
		const glm::vec3 &position, const Ref<EventBus> &eventBus, bool fractionalAtomPosition,
		OperatorRedoPanel *redoPanel, SceneOperatorRegistry *operatorRegistry, float bondRadiusMultiplier, SceneAddMenuSection section)
	{
		const auto addCurvedArrow = [&]()
		{
			if (redoPanel != nullptr && operatorRegistry != nullptr)
			{
				const SceneOperator *op = operatorRegistry->Find("scene.curved_arrow");
				if (op != nullptr)
				{
					const Result<void> result = redoPanel->RunAndOpen(*op, windowState);
					if (!result)
						DS_LOG_WARN("Add curved arrow failed: {}", result.Error().technicalDetails);
				}
			}
			else
			{
				const auto result = AddCurvedArrowThroughSelectedAtoms(windowState);
				if (!result)
					DS_LOG_WARN("Add curved arrow failed: {}", result.Error().technicalDetails);
			}
		};
		switch (section)
		{
		case SceneAddMenuSection::Drawing:
			DrawSceneDrawingAddItems(windowState, position, addCurvedArrow); return;
		case SceneAddMenuSection::Planes:
			DrawScenePlaneAddItems(windowState, position); return;
		case SceneAddMenuSection::Orbitals:
			DrawOrbitalAddMenu(windowState, position, false); return;
		case SceneAddMenuSection::Full: break;
		}
		const auto registry = commands.lock();
		const bool editable = registry != nullptr;
		if (ImGui::MenuItem("Atom...", nullptr, false, editable))
		{
			windowState.addAtomCoordinatesPopupPosition = position;
			windowState.addAtomCoordinatesPopupFractional = fractionalAtomPosition;
		}
		if (!editable && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
			ImGui::SetTooltip("Otwórz edytowalną strukturę.");
		ImGui::SeparatorText("Defekt");
		DrawDefectAddItems(windowState, registry.get(), position, bondRadiusMultiplier);
		ImGui::SeparatorText("Rysuj");
		DrawSceneDrawingAddItems(windowState, position, addCurvedArrow);
		DrawScenePlaneAddItems(windowState, position);
		DrawOrbitalAddMenu(windowState, position);
		if (ImGui::MenuItem("Tekst (swobodna etykieta)"))
		{
			PushPinnedMeasurementUndoSnapshot(windowState);
			RendererWindowState::FreeLabel label;
			label.id = windowState.sceneRegistry.AllocateObjectId();
			label.worldPosition = position;
			const auto id = label.id;
			windowState.freeLabels.push_back(std::move(label));
			SceneSystem::ClearStructureSelection(windowState.sceneRegistry, windowState);
			windowState.selectedPinnedMeasurements.clear();
			windowState.selectedSceneOrbitals.clear();
			windowState.selectedScenePlanes.clear();
			windowState.selectedScenePaths.clear();
			windowState.selectedVacancies.clear();
			windowState.defectFrameSelected = false;
			windowState.selectedFreeLabels = {id};
			SceneSystem::SyncLabelEntities(windowState.sceneRegistry, windowState);
			OpenViewportTextEditor(windowState, id, true);
		}
		if (ImGui::MenuItem("Narzędzie tekstu", "Shift+T", false, eventBus != nullptr))
		{
			RendererEvents::Viewport::SelectionToolToggleRequested event;
			event.windowId = windowState.windowId;
			event.tool = SelectionToolMode::Text;
			eventBus->Publish(event);
		}
		if (!eventBus && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
			ImGui::SetTooltip("Widok nie jest połączony z systemem zdarzeń.");
		if (ImGui::BeginMenu("Pomiary / etykiety", eventBus != nullptr))
		{
			std::size_t atoms = 0;
			for (const auto index : windowState.selectedAtomIndices)
				atoms += index < windowState.structure.atoms.size() ? 1u : 0u;
			auto item = [&]<typename Event>(std::string_view label, std::string_view shortcut, bool enabled, std::string_view hint) {
				if (ImGui::MenuItem(label.data(), shortcut.data(), false, enabled))
				{
					Event event;
					event.windowId = windowState.windowId;
					eventBus->Publish(event);
				}
				if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
					ImGui::SetTooltip("%s", hint.data());
			};
			item.operator()<RendererEvents::Viewport::LabelsToggleSelectedBondRequested>(
				"Długość wiązania", "M", atoms == 0 || atoms >= 2,
				"Zaznacz co najmniej dwa atomy. Bez zaznaczenia: narzędzie pomiaru, kliknij dwa atomy.");
			item.operator()<RendererEvents::Viewport::LabelsToggleSelectedAngleRequested>(
				"Kąt", "Shift+M", atoms == 0 || atoms >= 3,
				"Zaznacz co najmniej trzy atomy. Bez zaznaczenia: narzędzie pomiaru, kliknij trzy atomy.");
			item.operator()<RendererEvents::Viewport::LabelsShowAllBondRequested>(
				"Długości wszystkich widocznych wiązań", "Ctrl+M", !windowState.structure.bonds.empty(),
				"Wymaga widocznych wiązań w strukturze.");
			item.operator()<RendererEvents::Viewport::LabelsShowAllAngleRequested>(
				"Kąty wszystkich widocznych wiązań", "Ctrl+Shift+M", !windowState.structure.bonds.empty(),
				"Wymaga widocznych atomów tworzących kąty wiązań.");
			ImGui::EndMenu();
		}
		else if (!eventBus && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
			ImGui::SetTooltip("Widok nie jest połączony z systemem zdarzeń.");
	}
}
