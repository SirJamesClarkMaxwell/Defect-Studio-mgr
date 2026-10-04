#include "Core/dspch.hpp"

#include "Presentation/Panels/ViewportVacancySelection.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

#include <imgui.h>

#include "Core/Commands/CommandRegistry.hpp"
#include "Core/Logging/Logger.hpp"
#include "Presentation/Panels/ViewportDefectFrame.hpp"
#include "Presentation/Panels/ViewportVacancyAdd.hpp"
#include "Domain/DomainLayer.hpp"
#include "Renderer/AtomStyleTable.hpp"
#include "Renderer/Commands/RendererAtomEditCommands.hpp"
#include "Renderer/Commands/RendererVacancyCommands.hpp"
#include "Renderer/RendererLayer.hpp"
#include "Renderer/RendererViewCamera.hpp"
#include "Renderer/RendererWindowState.hpp"
#include "Renderer/Scene/SceneOrbitalGeometry.hpp"
#include "Renderer/Scene/SceneSystem.hpp"
#include "Renderer/Scene/SelectionHitTest.hpp"

namespace DefectStudio
{
	namespace
	{
		void DropStaleSelection(RendererWindowState &windowState)
		{
			std::erase_if(windowState.selectedVacancies,
				[&](const std::size_t index) { return index >= windowState.structure.vacancies.size(); });
		}

		// Replaces the domain vacancy list with `edit` applied to it - the one route every vacancy edit
		// takes (renderer.vacancy.set), so it is one undo step and reaches the sidecar.
		template <typename Edit>
		void EditDomainVacancies(RendererWindowState &windowState, RendererLayer &layer, DomainLayer *domain,
			CommandRegistry *registry, const char *description, Edit edit)
		{
			if (domain == nullptr || registry == nullptr)
				return;
			const auto target = ResolveAtomEditTarget(layer, *domain, windowState.windowId);
			if (!target)
			{
				DS_LOG_WARN("{} failed: {}", description, target.Error().technicalDetails);
				return;
			}
			SetVacanciesPayload payload{windowState.windowId, target->record->structure.vacancies, description};
			edit(payload.vacancies, target->record->structure);
			CommandContext context;
			context.Set<SetVacanciesPayload>(kSetVacanciesPayloadKey, std::move(payload));
			const auto result = registry->Execute(CommandID{kSetVacanciesCommandId}, std::move(context));
			if (!result)
				DS_LOG_WARN("{} failed: {}", description, result.Error().technicalDetails);
		}

		void RemoveSelected(RendererWindowState &windowState, RendererLayer &layer, DomainLayer *domain,
			CommandRegistry *registry)
		{
			std::vector<std::size_t> indices = windowState.selectedVacancies;
			std::sort(indices.rbegin(), indices.rend());
			EditDomainVacancies(windowState, layer, domain, registry, "Remove vacancy", [&](std::vector<VacancySite> &list, const CrystalStructure &) {
				for (const std::size_t index : indices)
					if (index < list.size())
						list.erase(list.begin() + static_cast<std::ptrdiff_t>(index));
			});
			windowState.selectedVacancies.clear();
		}
	} // namespace

	std::optional<std::size_t> PickVacancyAlongRay(
		const RendererWindowState &windowState, const glm::vec3 &rayOrigin, const glm::vec3 &rayDirection)
	{
		const float length = glm::length(rayDirection);
		if (!windowState.showVacancies || !std::isfinite(length) || length <= 0.0f)
			return std::nullopt;
		const glm::vec3 direction = rayDirection / length;
		std::optional<std::size_t> hit;
		float best = std::numeric_limits<float>::max();
		for (std::size_t index = 0; index < windowState.structure.vacancies.size(); ++index)
		{
			const RendererVacancyData &vacancy = windowState.structure.vacancies[index];
			const glm::vec3 toCentre = vacancy.cartesianPosition - rayOrigin;
			const float along = glm::dot(toCentre, direction);
			const float radius = vacancy.radius * 1.35f;
			if (along <= 0.0f || glm::dot(toCentre, toCentre) - along * along > radius * radius)
				continue;
			if (along < best)
			{
				best = along;
				hit = index;
			}
		}
		return hit;
	}

	bool HandleVacancyInteraction(
		RendererWindowState &windowState, const ImVec2 &imageOrigin, const ImVec2 &imageSize, bool hovered)
	{
		DropStaleSelection(windowState);
		if ((!windowState.pickAtoms && !windowState.pickLabels) || windowState.camera == nullptr ||
			(windowState.structure.vacancies.empty() && !windowState.structure.defectFrame) || !hovered || !ImGui::IsMouseClicked(ImGuiMouseButton_Left) ||
			imageSize.x <= 0.0f || imageSize.y <= 0.0f)
			return false;
		const ImVec2 mouse = ImGui::GetMousePos();
		if (IsDefectFrameUnderMouse(windowState, imageOrigin, imageSize, mouse))
		{
			const bool additive = ImGui::GetIO().KeyCtrl;
			windowState.defectFrameSelected = additive ? !windowState.defectFrameSelected : true;
			// Ctrl keeps the rest of the selection: selected objects + axes move together about the axes.
			if (!additive)
			{
				windowState.selectedVacancies.clear();
				SceneSystem::ClearStructureSelection(windowState.sceneRegistry, windowState);
				windowState.selectedFreeLabels.clear();
				windowState.selectedSceneArrows.clear();
				windowState.selectedPinnedMeasurements.clear();
				windowState.selectedSceneOrbitals.clear();
				windowState.selectedScenePlanes.clear();
				windowState.selectedScenePaths.clear();
			}
			return true;
		}
		const float ndcX = 2.0f * (mouse.x - imageOrigin.x) / imageSize.x - 1.0f;
		const float ndcY = 1.0f - 2.0f * (mouse.y - imageOrigin.y) / imageSize.y;
		if (std::abs(ndcX) > 1.0f || std::abs(ndcY) > 1.0f)
			return false;
		const glm::mat4 inverse =
			glm::inverse(windowState.camera->ProjectionMatrix() * windowState.camera->ViewMatrix());
		const glm::vec4 nearPoint = inverse * glm::vec4(ndcX, ndcY, -1.0f, 1.0f);
		const glm::vec4 farPoint = inverse * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);
		if (std::abs(nearPoint.w) < 1e-6f || std::abs(farPoint.w) < 1e-6f)
			return false;
		const glm::vec3 origin = glm::vec3(nearPoint) / nearPoint.w;
		const auto hit = PickVacancyAlongRay(windowState, origin, glm::vec3(farPoint) / farPoint.w - origin);
		if (!hit)
			return false;

		auto &selection = windowState.selectedVacancies;
		if (ImGui::GetIO().KeyCtrl)
		{
			const auto found = std::find(selection.begin(), selection.end(), *hit);
			if (found == selection.end())
				selection.push_back(*hit);
			else
				selection.erase(found);
		}
		else
		{
			selection = {*hit};
			windowState.defectFrameSelected = false;
			SceneSystem::ClearStructureSelection(windowState.sceneRegistry, windowState);
		}
		windowState.selectedFreeLabels.clear();
		windowState.selectedSceneArrows.clear();
		windowState.selectedPinnedMeasurements.clear();
		windowState.selectedSceneOrbitals.clear();
		windowState.selectedScenePlanes.clear();
		windowState.selectedScenePaths.clear();
		return true;
	}

	void HandleVacancyKeyboardShortcuts(RendererWindowState &windowState, bool hovered, RendererLayer &layer,
		DomainLayer *domain, CommandRegistry *registry)
	{
		DropStaleSelection(windowState);
		if (!hovered || windowState.pathEdit.IsActive() || !ImGui::IsKeyPressed(ImGuiKey_Delete, false))
			return;
		if (!windowState.selectedVacancies.empty())
			RemoveSelected(windowState, layer, domain, registry);
		if (windowState.defectFrameSelected && windowState.structure.defectFrame && registry != nullptr)
		{
			windowState.defectFrameSelected = false;
			CommandContext context;
			context.Set<SetDefectFramePayload>(
				kSetDefectFramePayloadKey, SetDefectFramePayload{windowState.windowId, std::nullopt, "Remove defect axes"});
			const auto result = registry->Execute(CommandID{kSetDefectFrameCommandId}, std::move(context));
			if (!result)
				DS_LOG_WARN("Remove defect axes failed: {}", result.Error().technicalDetails);
		}
	}

	void ApplyCentreRegionSelection(RendererWindowState &windowState, const std::function<bool(glm::vec2)> &inside,
		const bool replace, const bool subtract)
	{
		if (windowState.camera == nullptr)
			return;
		if (replace)
		{
			windowState.selectedSceneOrbitals.clear();
			windowState.selectedVacancies.clear();
			windowState.defectFrameSelected = false;
		}
		const glm::mat4 viewProjection = windowState.camera->ProjectionMatrix() * windowState.camera->ViewMatrix();
		auto hit = [&](const glm::vec3 &world) {
			const auto screen = SelectionHitTest::ProjectToScreen(viewProjection, windowState.viewportSize, world);
			return screen.has_value() && inside(*screen);
		};
		auto apply = [subtract](auto &selection, const auto &value) {
			const auto existing = std::find(selection.begin(), selection.end(), value);
			if (subtract && existing != selection.end())
				selection.erase(existing);
			else if (!subtract && existing == selection.end())
				selection.push_back(value);
		};
		if (windowState.pickLabels)
			for (const auto &orbital : windowState.sceneOrbitals)
				if (orbital.visible && hit(SceneOrbitalWorldBounds(orbital, windowState.structure).center))
					apply(windowState.selectedSceneOrbitals, orbital.id);
		if ((windowState.pickAtoms || windowState.pickLabels) && windowState.showVacancies)
			for (std::size_t index = 0; index < windowState.structure.vacancies.size(); ++index)
				if (hit(windowState.structure.vacancies[index].cartesianPosition))
					apply(windowState.selectedVacancies, index);
	}

	void DrawSelectedVacancyOverlay(
		const RendererWindowState &windowState, const ImVec2 &imageOrigin, const ImVec2 &imageSize)
	{
		if (windowState.selectedVacancies.empty() || !windowState.showVacancies || windowState.camera == nullptr)
			return;
		const glm::mat4 viewProjection = windowState.camera->ProjectionMatrix() * windowState.camera->ViewMatrix();
		const glm::mat4 view = windowState.camera->ViewMatrix();
		const glm::vec3 cameraRight(view[0][0], view[1][0], view[2][0]);
		auto project = [&](const glm::vec3 &world, ImVec2 &out) {
			const glm::vec4 clip = viewProjection * glm::vec4(world, 1.0f);
			if (clip.w <= 0.0001f)
				return false;
			out = ImVec2(imageOrigin.x + (clip.x / clip.w * 0.5f + 0.5f) * imageSize.x,
				imageOrigin.y + (0.5f - clip.y / clip.w * 0.5f) * imageSize.y);
			return true;
		};
		ImDrawList &drawList = *ImGui::GetWindowDrawList();
		for (const std::size_t index : windowState.selectedVacancies)
		{
			if (index >= windowState.structure.vacancies.size())
				continue;
			const RendererVacancyData &vacancy = windowState.structure.vacancies[index];
			ImVec2 centre, edge;
			if (!project(vacancy.cartesianPosition, centre) ||
				!project(vacancy.cartesianPosition + cameraRight * vacancy.radius * 1.2f, edge))
				continue;
			const float radius = std::max(6.0f, std::hypot(edge.x - centre.x, edge.y - centre.y));
			drawList.AddCircle(centre, radius, IM_COL32(255, 200, 60, 255), 48, 2.5f);
		}
	}

	bool DrawVacancyStyleEditor(AtomStyleTable &styles, RendererLayer &layer)
	{
		VacancyRenderStyle style = styles.GetVacancyStyle();
		bool changed = ImGui::Checkbox("Kolor usuniętego pierwiastka##vacancy", &style.colorBySpecies);
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Wakans po usuniętym atomie ma kolor tego pierwiastka (V_B / V_N w hBN).\n"
							  "Kolor poniżej: wakanse bez pierwiastka. Własny kolor: Object Properties wakansu.");
		changed |= ImGui::ColorEdit3("Color##vacancy", &style.color.x);
		changed |= ImGui::DragFloat("Radius##vacancy", &style.displayRadius, 0.01f, 0.05f, 3.0f, "%.2f");
		changed |= ImGui::SliderFloat("Opacity##vacancy", &style.opacity, 0.0f, 1.0f);
		int mode = static_cast<int>(style.renderMode);
		changed |= ImGui::Combo("Mode##vacancy", &mode, "Ghost\0Wireframe\0Solid\0");
		style.renderMode = static_cast<VacancyRenderMode>(mode);
		changed |= ImGui::SliderInt("Dashes##vacancy", &style.dashCount, 0, 64);
		changed |= ImGui::DragFloat("Ring width##vacancy", &style.ringWidth, 0.005f, 0.005f, 3.0f, "%.3f");
		if (!changed)
			return false;
		styles.SetVacancyStyle(style);
		// Like element styles, update the baked renderer copies in every open window.
		for (RendererWindowState &window : layer.GetWindows())
			for (RendererVacancyData &vacancy : window.structure.vacancies)
			{
				if (!vacancy.customColor)
					vacancy.color = styles.VacancyColor(vacancy.sourceSpecies);
				vacancy.radius = style.displayRadius;
				vacancy.opacity = style.opacity;
				vacancy.renderMode = style.renderMode;
				vacancy.dashCount = style.dashCount;
				vacancy.ringWidth = style.ringWidth;
			}
		return true;
	}

	void DrawSelectedVacancySection(RendererWindowState &windowState, RendererLayer &layer, DomainLayer *domain,
		CommandRegistry *registry, AtomStyleTable *styles)
	{
		DropStaleSelection(windowState);
		if (windowState.selectedVacancies.empty())
			return;
		ImGui::PushID("SelectedVacancies");
		ImGui::SeparatorText(windowState.selectedVacancies.size() == 1 ? "Wakans" : "Wakanse");
		if (windowState.selectedVacancies.size() == 1)
		{
			const std::size_t index = windowState.selectedVacancies.front();
			RendererVacancyData &vacancy = windowState.structure.vacancies[index];
			glm::vec3 position = vacancy.cartesianPosition;
			if (ImGui::DragFloat3("Pozycja (A)", &position.x, 0.01f, 0.0f, 0.0f, "%.3f"))
				vacancy.cartesianPosition = position;
			if (ImGui::IsItemDeactivatedAfterEdit())
			{
				const glm::vec3 moved = vacancy.cartesianPosition;
				EditDomainVacancies(windowState, layer, domain, registry, "Move vacancy",
					[&](std::vector<VacancySite> &list, const CrystalStructure &structure) {
						if (index < list.size())
						{
							list[index].position = moved;
							list[index].fractional = structure.CartesianToFractional(moved);
						}
					});
			}
			ImGui::TextDisabled("G albo gizmo: przesuń wakans w widoku.");
			static char labelBuffer[32] = "";
			static std::size_t bufferIndex = std::numeric_limits<std::size_t>::max();
			// ponytail: one shared buffer, re-seeded when another vacancy is selected.
			if (bufferIndex != index)
			{
				std::snprintf(labelBuffer, sizeof(labelBuffer), "%s", vacancy.label.c_str());
				bufferIndex = index;
			}
			ImGui::InputText("Etykieta", labelBuffer, sizeof(labelBuffer));
			if (ImGui::IsItemDeactivatedAfterEdit())
			{
				const std::string label = labelBuffer;
				EditDomainVacancies(windowState, layer, domain, registry, "Rename vacancy",
					[&](std::vector<VacancySite> &list, const CrystalStructure &) {
						if (index < list.size())
							list[index].label = label;
					});
			}
		}
		else
			ImGui::Text("%zu zaznaczonych", windowState.selectedVacancies.size());

		// Colour of just the selected vacancies (V_B vs V_N in hBN): live on the markers while the
		// picker is dragged, one undo step on release.
		const std::vector<std::size_t> selected = windowState.selectedVacancies;
		auto setDomainColor = [&](std::optional<glm::vec3> color) {
			EditDomainVacancies(windowState, layer, domain, registry, "Vacancy colour",
				[&](std::vector<VacancySite> &list, const CrystalStructure &) {
					for (const std::size_t index : selected)
						if (index < list.size())
							list[index].color = color;
				});
		};
		const RendererVacancyData &first = windowState.structure.vacancies[selected.front()];
		bool ownColor = first.customColor;
		if (ImGui::Checkbox("Własny kolor", &ownColor))
			setDomainColor(ownColor ? std::optional<glm::vec3>(first.color) : std::nullopt);
		if (first.customColor)
		{
			ImGui::SameLine();
			glm::vec3 color = first.color;
			if (ImGui::ColorEdit3("##vacancyOwnColor", &color.x, ImGuiColorEditFlags_NoInputs))
				for (const std::size_t index : selected)
					windowState.structure.vacancies[index].color = color;
			if (ImGui::IsItemDeactivatedAfterEdit())
				setDomainColor(windowState.structure.vacancies[selected.front()].color);
		}
		if (ImGui::Button("Wiązania do sąsiadów"))
		{
			SceneSystem::ClearStructureSelection(windowState.sceneRegistry, windowState);
			(void)AddVacancyBonds(windowState);
		}
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Dwukolorowa linia atom -> wakans do każdego atomu pierwszej sfery.\n"
							  "Każda jest osobną linią sceny: zaznacz ją, by zmienić styl.");
		if (ImGui::Button("Usuń (Del)"))
			RemoveSelected(windowState, layer, domain, registry);

		ImGui::SeparatorText("Wygląd wszystkich wakansów");
		if (styles != nullptr)
			DrawVacancyStyleEditor(*styles, layer);
		else
			ImGui::TextDisabled("Edytuj w panelu Object Properties lub Element Catalog > Wakans.");
		ImGui::PopID();
	}
} // namespace DefectStudio
