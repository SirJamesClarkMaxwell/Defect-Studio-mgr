#include "Core/dspch.hpp"

#include "Presentation/Panels/ViewportVacancyAdd.hpp"

#include <limits>
#include <optional>
#include <utility>

#include <imgui.h>

#include "Core/Commands/CommandRegistry.hpp"
#include "Core/Logging/Logger.hpp"
#include "Domain/Defects/DefectModel.hpp"
#include "Presentation/Panels/SceneArrowEditorWidget.hpp"
#include "Presentation/Panels/ViewportDefectFrame.hpp"
#include "Renderer/Commands/RendererVacancyCommands.hpp"
#include "Renderer/RendererLayer.hpp"
#include "Renderer/RendererWindowState.hpp"
#include "Renderer/Scene/SceneSystem.hpp"

namespace DefectStudio
{
	namespace
	{
		constexpr float kShellTolerance = 1.15f;

		[[nodiscard]] std::optional<glm::vec3> SelectedAtomCentroid(const RendererWindowState &windowState)
		{
			glm::vec3 sum(0.0f);
			std::size_t count = 0;
			for (const std::size_t index : windowState.selectedAtomIndices)
				if (index < windowState.structure.atoms.size())
				{
					sum += windowState.structure.atoms[index].cartesianPosition;
					++count;
				}
			// ponytail: plain mean across cell boundaries; unwrap if boundary selections need it.
			return count == 0 ? std::nullopt : std::optional<glm::vec3>(sum / static_cast<float>(count));
		}

		[[nodiscard]] std::optional<std::size_t> NearestVacancy(const RendererWindowState &windowState, const glm::vec3 &position)
		{
			std::optional<std::size_t> nearest;
			float best = std::numeric_limits<float>::max();
			for (std::size_t index = 0; index < windowState.structure.vacancies.size(); ++index)
			{
				const float distance = glm::length(windowState.structure.vacancies[index].cartesianPosition - position);
				if (distance < best)
				{
					best = distance;
					nearest = index;
				}
			}
			return nearest;
		}

		[[nodiscard]] RendererWindowState::SceneArrow MakeVacancyBond(
			const RendererWindowState &windowState, std::size_t atomIndex, const RendererVacancyData &vacancy)
		{
			const RendererAtomData &atom = windowState.structure.atoms[atomIndex];
			RendererWindowState::SceneArrow arrow = MakeDefaultSceneArrow(windowState, atom.cartesianPosition);
			arrow.points = {atom.cartesianPosition, vacancy.cartesianPosition};
			ApplySceneArrowKindChange(arrow, RendererWindowState::ArrowKind::Line);
			arrow.startTip = RendererWindowState::ArrowTip::None;
			arrow.endTip = RendererWindowState::ArrowTip::None;
			arrow.startAnchorAtom = atomIndex;
			arrow.atomBuffer = 0.0f;
			// Same thickness as the structure's own bonds (ponytail: ignores the bond radius multiplier).
			const float bondRadius = windowState.structure.bonds.empty() ? 0.09f : windowState.structure.bonds.front().radius;
			arrow.style.shaftWidth = 2.0f * bondRadius;
			arrow.style.useGradient = true;
			arrow.style.gradient.start = atom.color;
			arrow.style.gradient.finish = vacancy.color;
			return arrow;
		}
	} // namespace

	void AddVacancyAt(RendererWindowState &windowState, CommandRegistry *registry, const glm::vec3 &position)
	{
		if (registry == nullptr)
			return;
		SetVacanciesPayload payload{windowState.windowId, {}, "Add vacancy"};
		payload.edit = [position](std::vector<VacancySite> &list, const CrystalStructure &structure) {
			list.push_back(MakeVacancySite(structure, position));
		};
		CommandContext context;
		context.Set<SetVacanciesPayload>(kSetVacanciesPayloadKey, std::move(payload));
		const auto result = registry->Execute(CommandID{kSetVacanciesCommandId}, std::move(context));
		if (!result)
			DS_LOG_WARN("Add vacancy failed: {}", result.Error().technicalDetails);
	}

	std::vector<std::size_t> NeighbourShell(const RendererWindowState &windowState, const glm::vec3 &position)
	{
		const auto &atoms = windowState.structure.atoms;
		float nearest = std::numeric_limits<float>::max();
		for (const RendererAtomData &atom : atoms)
			if (atom.visible)
			{
				const float distance = glm::length(atom.cartesianPosition - position);
				if (distance > 1.0e-3f)
					nearest = std::min(nearest, distance);
			}
		std::vector<std::size_t> shell;
		for (std::size_t index = 0; index < atoms.size(); ++index)
		{
			const float distance = glm::length(atoms[index].cartesianPosition - position);
			if (atoms[index].visible && distance > 1.0e-3f && distance <= nearest * kShellTolerance)
				shell.push_back(index);
		}
		return shell;
	}

	std::size_t AddVacancyBonds(RendererWindowState &windowState)
	{
		const auto &vacancies = windowState.structure.vacancies;
		std::vector<std::size_t> atoms;
		for (const std::size_t atom : windowState.selectedAtomIndices)
			if (atom < windowState.structure.atoms.size())
				atoms.push_back(atom);
		std::vector<std::pair<std::size_t, std::size_t>> pairs; // atom, vacancy
		// Stale atom indices (atoms just deleted to make the vacancies) do not count as a selection.
		if (!atoms.empty())
		{
			for (const std::size_t atom : atoms)
			{
				const std::optional<std::size_t> vacancy = windowState.selectedVacancies.size() == 1
					? std::optional<std::size_t>(windowState.selectedVacancies.front())
					: NearestVacancy(windowState, windowState.structure.atoms[atom].cartesianPosition);
				if (vacancy && *vacancy < vacancies.size())
					pairs.emplace_back(atom, *vacancy);
			}
		}
		else
		{
			std::vector<std::size_t> chosen;
			for (const std::size_t vacancy : windowState.selectedVacancies)
				if (vacancy < vacancies.size())
					chosen.push_back(vacancy);
			for (std::size_t index = 0; chosen.empty() && index < vacancies.size(); ++index)
				chosen.push_back(index); // nothing selected: every vacancy
			for (const std::size_t vacancy : chosen)
				for (const std::size_t atom : NeighbourShell(windowState, vacancies[vacancy].cartesianPosition))
					pairs.emplace_back(atom, vacancy);
		}
		DS_LOG_INFO("Vacancy bonds: {} line(s) from {} selected atom(s), {} selected vacancy(ies), {} vacancy(ies) in total",
			pairs.size(), atoms.size(), windowState.selectedVacancies.size(), vacancies.size());
		if (pairs.empty())
			return 0;

		PushPinnedMeasurementUndoSnapshot(windowState);
		std::vector<SceneObjectId> added;
		for (const auto &[atom, vacancy] : pairs)
			added.push_back(SceneSystem::AppendSceneArrow(windowState, MakeVacancyBond(windowState, atom, vacancies[vacancy])));
		// The new lines become the selection, so Object Properties shows their style right away.
		windowState.selectedSceneArrows = std::move(added);
		windowState.selectedVacancies.clear();
		SceneSystem::ClearStructureSelection(windowState.sceneRegistry, windowState);
		SceneSystem::SyncLabelEntities(windowState.sceneRegistry, windowState);
		return pairs.size();
	}

	void DrawDefectAddItems(RendererWindowState &windowState, CommandRegistry *registry, const glm::vec3 &position)
	{
		const bool editable = !windowState.structure.domainStructureId.empty();
		const std::optional<glm::vec3> centroid = SelectedAtomCentroid(windowState);
		if (ImGui::MenuItem(centroid ? "Vacancy (selection centroid)" : "Vacancy", nullptr, false, editable))
			AddVacancyAt(windowState, registry, centroid.value_or(position));
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Wakans w środku zaznaczonych atomów, a bez zaznaczenia w miejscu kliknięcia / kursora 3D.");

		const bool canBond = !windowState.structure.vacancies.empty();
		if (ImGui::MenuItem("Vacancy bonds", nullptr, false, canBond))
			(void)AddVacancyBonds(windowState);
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
			ImGui::SetTooltip("Zaznaczone atomy -> wakans (zaznaczony albo najbliższy),\n"
							  "albo zaznaczone wakanse (bez zaznaczenia: wszystkie) -> pierwsza sfera sąsiadów.\n"
							  "Dwukolorowe linie atom -> wakans, styl każdej osobno.");

		DrawDefectFrameAddMenu(windowState, registry, position);
	}
} // namespace DefectStudio
