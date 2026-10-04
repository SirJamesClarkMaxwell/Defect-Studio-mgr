#include "Core/dspch.hpp"

#include "Presentation/Panels/ViewportVacancyAdd.hpp"

#include <algorithm>
#include <limits>
#include <optional>
#include <utility>

#include <imgui.h>

#include "Core/Commands/CommandRegistry.hpp"
#include "Core/Logging/Logger.hpp"
#include "Domain/Defects/DefectModel.hpp"
#include "Presentation/Panels/ScenePathDevMenu.hpp"
#include "Presentation/Panels/ScenePathOperations.hpp"
#include "Renderer/Path/PathTopology.hpp"
#include "Presentation/Panels/ViewportDefectFrame.hpp"
#include "Renderer/Commands/RendererVacancyCommands.hpp"
#include "Renderer/RendererLayer.hpp"
#include "Renderer/RendererWindowState.hpp"
#include "Renderer/Scene/SceneSystem.hpp"
#include "Renderer/Scene/SceneFreeLabelAnchors.hpp"

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

		[[nodiscard]] ScenePath MakeBondLine(const RendererWindowState &windowState, const glm::vec3 &start,
			const glm::vec3 &end, const glm::vec3 &startColor, const glm::vec3 &endColor)
		{
			ScenePath path = MakeDevScenePath(ScenePathDevPreset::Line, glm::vec3(0));
			path.name = "Vacancy bond";
			path.transform.position = glm::vec3(0);
			path.nodes[0].position = start;
			path.nodes[1].position = end;
			path.style.startDecoration.kind = PathDecorationKind::None;
			path.style.endDecoration.kind = PathDecorationKind::None;
			// Same thickness as the structure's own bonds (ponytail: ignores the bond radius multiplier).
			const float bondRadius = windowState.structure.bonds.empty() ? 0.09f : windowState.structure.bonds.front().radius;
			path.style.width = 2.0f * bondRadius;
			path.style.gradient.enabled = true;
			path.style.gradient.stops = {{0.0f, startColor, 1.0f}, {1.0f, endColor, 1.0f}};
			return path;
		}

		[[nodiscard]] ScenePath MakeVacancyBond(
			const RendererWindowState &windowState, std::size_t atomIndex, std::size_t vacancyIndex)
		{
			const RendererAtomData &atom = windowState.structure.atoms[atomIndex];
			const RendererVacancyData &vacancy = windowState.structure.vacancies[vacancyIndex];
			ScenePath path = MakeBondLine(windowState, atom.cartesianPosition, vacancy.cartesianPosition,
				atom.color, vacancy.color);
			path.nodes[0].binding = PathBinding{PathBinding::CopyPosition{atomIndex, {}, 0.0f}};
			path.nodes[1].binding = PathBinding{PathBinding::CopyVacancy{vacancyIndex, {}, 1.0f}};
			MovePathOriginToCentre(path);
			return path;
		}

		[[nodiscard]] ScenePath MakeVacancyPairBond(
			const RendererWindowState &windowState, std::size_t firstIndex, std::size_t secondIndex)
		{
			const RendererVacancyData &first = windowState.structure.vacancies[firstIndex];
			const RendererVacancyData &second = windowState.structure.vacancies[secondIndex];
			ScenePath path = MakeBondLine(windowState, first.cartesianPosition,
				second.cartesianPosition, first.color, second.color);
			path.nodes[0].binding = PathBinding{PathBinding::CopyVacancy{firstIndex, {}, 1.0f}};
			path.nodes[1].binding = PathBinding{PathBinding::CopyVacancy{secondIndex, {}, 1.0f}};
			MovePathOriginToCentre(path);
			return path;
		}

		[[nodiscard]] float NearestShownAtomDistance(const RendererWindowState &windowState, const glm::vec3 &position)
		{
			float nearest = std::numeric_limits<float>::max();
			for (const RendererAtomData &atom : windowState.structure.atoms)
				if (atom.visible)
				{
					const float distance = glm::length(atom.cartesianPosition - position);
					if (distance > 1.0e-3f)
						nearest = std::min(nearest, distance);
				}
			return nearest;
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
		const float nearest = NearestShownAtomDistance(windowState, position);
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
		std::vector<std::pair<std::size_t, std::size_t>> vacancyPairs; // vacancy, vacancy (first < second)
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
			// Two chosen vacancies that are first-shell neighbours (V_B - V_N in hBN) get a line too.
			for (std::size_t a = 0; a < chosen.size(); ++a)
				for (std::size_t b = a + 1; b < chosen.size(); ++b)
				{
					const glm::vec3 &first = vacancies[chosen[a]].cartesianPosition;
					const float shell = NearestShownAtomDistance(windowState, first) * kShellTolerance;
					if (glm::length(vacancies[chosen[b]].cartesianPosition - first) <= shell)
						vacancyPairs.emplace_back(std::min(chosen[a], chosen[b]), std::max(chosen[a], chosen[b]));
				}
		}
		DS_LOG_INFO("Vacancy bonds: {} line(s) from {} selected atom(s), {} selected vacancy(ies), {} vacancy(ies) in total",
			pairs.size(), atoms.size(), windowState.selectedVacancies.size(), vacancies.size());
		if (pairs.empty() && vacancyPairs.empty())
			return 0;

		const SceneObjectsSnapshot before = CaptureSceneObjectsSnapshot(windowState);
		std::vector<SceneObjectId> added;
		for (const auto &[atom, vacancy] : pairs)
		{
			const auto result = AddScenePath(MakeSilentPathEditContext(windowState), MakeVacancyBond(windowState, atom, vacancy));
			if (result)
				added.push_back(result.Value());
			else
				DS_LOG_WARN("Add vacancy bond failed: {}", result.Error().technicalDetails);
		}
		for (const auto &[first, second] : vacancyPairs)
		{
			const auto result = AddScenePath(
				MakeSilentPathEditContext(windowState), MakeVacancyPairBond(windowState, first, second));
			if (result)
				added.push_back(result.Value());
			else
				DS_LOG_WARN("Add vacancy bond failed: {}", result.Error().technicalDetails);
		}
		if (added.empty())
			return 0;
		PushSceneObjectsUndoSnapshot(windowState, before);
		const std::size_t count = added.size();
		// The new lines become the selection, so Object Properties shows their style right away.
		SelectAddedScenePaths(windowState, std::move(added));
		return count;
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

		if (ImGui::MenuItem("Vacancy labels", nullptr, false, !windowState.structure.vacancies.empty()))
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

		DrawDefectFrameAddMenu(windowState, registry, position);
	}
} // namespace DefectStudio
