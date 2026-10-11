#include "Core/dspch.hpp"

#include "Presentation/Panels/FnvCorrectionPanel.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <format>

#include <imgui.h>

#include "Core/JobSystem/JobSystem.hpp"
#include "Core/Platform/FileDialog.hpp"
#include "Presentation/Panels/FnvCorrectionInputs.hpp"
#include "Presentation/Panels/PropertyGrid.hpp"
#include "Renderer/RendererLayer.hpp"
#include "Renderer/Scene/SceneDensityAnalysis.hpp"
#include "Renderer/Scene/SceneFreeLabelAnchors.hpp"
#include "Renderer/Scene/SceneSystem.hpp"

namespace DefectStudio
{
	namespace
	{
		constexpr ImVec4 kWarningColor(0.95f, 0.75f, 0.30f, 1.0f);

		bool DrawPathField(const char *id, Path &path, std::string &error)
		{
			ImGui::PushID(id);
			const float buttonWidth = ImGui::GetFrameHeight();
			const float spacing = ImGui::GetStyle().ItemInnerSpacing.x;
			const float fieldWidth = std::max(1.0f, ImGui::GetContentRegionAvail().x - buttonWidth - spacing);
			const std::string full = path.Empty() ? "brak" : path.Utf8();
			std::string shown = full;
			const float textWidth = std::max(1.0f, fieldWidth - 2.0f * ImGui::GetStyle().FramePadding.x);
			// Walk whole UTF-8 code points so left truncation also works for Polish folder names.
			for (std::size_t offset = 0; ImGui::CalcTextSize(shown.c_str()).x > textWidth && offset < full.size();)
			{
				++offset;
				while (offset < full.size() && (static_cast<unsigned char>(full[offset]) & 0xc0) == 0x80)
					++offset;
				shown = "..." + full.substr(offset);
			}
			ImGui::SetNextItemWidth(fieldWidth);
			ImGui::InputText("##path", shown.data(), shown.size() + 1, ImGuiInputTextFlags_ReadOnly);
			ImGui::SetItemTooltip("%s", full.c_str());
			ImGui::SameLine(0.0f, spacing);
			bool changed = false;
			if (ImGui::Button("...", ImVec2(buttonWidth, 0.0f)))
			{
				const auto picked = Platform::PickOpenFile(path.Empty() ? Path() : path.parent_path(), "", "");
				if (!picked)
					error = picked.Error().userMessage;
				else if (picked->has_value() && **picked != path)
				{
					path = **picked;
					changed = true;
				}
			}
			ImGui::SetItemTooltip("Wybierz plik LOCPOT");
			ImGui::PopID();
			return changed;
		}
	} // namespace

	FnvCorrectionPanel::FnvCorrectionPanel(RendererLayer &rendererLayer, WeakRef<JobSystem> jobSystem,
		std::string title, const bool visibleByDefault)
		: IPanel(std::move(title), visibleByDefault), m_RendererLayer(rendererLayer), m_JobSystem(std::move(jobSystem))
	{
		m_Request.dielectric = 5.7;
		std::snprintf(m_DielectricSource.data(), m_DielectricSource.size(), "%s", "promotor (diament)");
	}

	Ref<IPanel> FnvCorrectionPanel::Clone() const
	{
		auto clone = CreateRef<FnvCorrectionPanel>(m_RendererLayer, m_JobSystem, GetTitle(), IsVisible());
		clone->m_SaveHandler = m_SaveHandler;
		clone->m_Saved = m_Saved;
		clone->m_Request = m_Request;
		clone->m_DielectricSource = m_DielectricSource;
		clone->m_Profiles = m_Profiles;
		clone->m_Convention = m_Convention;
		clone->m_UseAxis = m_UseAxis;
		clone->m_Alignments = m_Alignments;
		clone->m_ModelPotential = m_ModelPotential;
		clone->m_AxisLabels = m_AxisLabels;
		clone->m_CentreSource = m_CentreSource;
		clone->m_ShowDeltaV = m_ShowDeltaV;
		clone->m_ShowModel = m_ShowModel;
		clone->m_EditedAxis = m_EditedAxis;
		return clone;
	}

	void FnvCorrectionPanel::invalidateProfiles()
	{
		++m_InputRevision;
		m_Dirty = true;
		m_Profiles.reset();
		m_Error.clear();
	}

	void FnvCorrectionPanel::submitProfiles()
	{
		const auto jobSystem = m_JobSystem.lock();
		if (!jobSystem)
		{
			m_Error = "JobSystem niedostępny.";
			return;
		}
		m_PendingJob = CreateRef<FnvCorrectionJob>(m_Request);
		m_SubmittedRevision = m_InputRevision;
		m_PendingJobId = jobSystem->Submit(m_PendingJob, JobPriority::Normal);
		m_Error.clear();
	}

	void FnvCorrectionPanel::pollJob()
	{
		if (!m_PendingJob)
			return;
		const auto jobSystem = m_JobSystem.lock();
		const auto snapshot = jobSystem ? jobSystem->GetJob(m_PendingJobId) : std::nullopt;
		if (snapshot && (snapshot->status == JobStatus::Queued || snapshot->status == JobStatus::Running))
			return;
		if (m_SubmittedRevision == m_InputRevision)
		{
			if (snapshot && snapshot->status == JobStatus::Completed && m_PendingJob->GetResult())
			{
				m_Profiles = std::move(*m_PendingJob->GetResult());
				proposeWindows();
			}
			else
				m_Error = snapshot && !snapshot->errorMessage.empty() ? snapshot->errorMessage : "Obliczanie profili nie powiodło się.";
		}
		m_PendingJob.reset();
		m_PendingJobId = 0;
	}

	void FnvCorrectionPanel::proposeCentre()
	{
		if (m_Request.defectLocpot.Empty() || m_Request.bulkLocpot.Empty())
			return;
		const auto defect = ReadLocpotStructure(m_Request.defectLocpot);
		const auto bulk = ReadLocpotStructure(m_Request.bulkLocpot);
		const auto centre = defect && bulk ? FindImpurityCentre(*defect, *bulk) : std::nullopt;
		if (!centre)
		{
			m_CentreSource = "brak domieszki w strukturze - podaj środek";
			return;
		}
		m_Request.centreFractional = centre->fractional;
		m_CentreSource = "auto: centroid " + centre->species;
	}

	void FnvCorrectionPanel::proposeWindows()
	{
		m_Dirty = true;
		rebuildAlignments(std::nullopt);
	}

	void FnvCorrectionPanel::rebuildAlignments(const std::optional<std::array<FnvPlateauWindow, 3>> &windows)
	{
		if (!m_Profiles)
			return;
		const auto structure = ReadLocpotStructure(m_Request.defectLocpot);
		m_AxisLabels = structure ? FnvAxisLabels(structure->lattice) : std::array<std::string, 3>{"a1", "a2", "a3"};
		for (int a = 0; a < 3; ++a)
		{
			const auto &axis = m_Profiles->axes[a];
			m_ModelPotential[a] = FnvModelPotential(*m_Profiles, a, m_Convention);
			const auto finite = [](const std::vector<double> &values) {
				return std::all_of(values.begin(), values.end(), [](double value) { return std::isfinite(value); });
			};
			// The bridge validates its arrays; also guard generated positions and the convention shift
			// before calling Domain's throwing validation on the render path.
			if (!std::isfinite(axis.length) || axis.length <= 0.0 || axis.position.size() < 2 ||
				axis.position.size() != axis.deltaV.size() || axis.position.size() != m_ModelPotential[a].size() ||
				!finite(axis.position) || !finite(axis.deltaV) || !finite(m_ModelPotential[a]) ||
				!(axis.position[1] > axis.position[0]) ||
				(windows && (!std::isfinite((*windows)[a].centre) || !std::isfinite((*windows)[a].width) ||
					(*windows)[a].width < 0.0)))
			{
				m_Error = "Nieprawidłowe profile FNV.";
				m_Profiles.reset();
				return;
			}
			m_Alignments[a] = windows ? AlignFnvInWindow(axis, m_ModelPotential[a], (*windows)[a]) :
				ProposeFnvAlignment(axis, m_ModelPotential[a]);
		}
	}

	void FnvCorrectionPanel::drawData()
	{
		if (ImGui::CollapsingHeader("Dane", ImGuiTreeNodeFlags_DefaultOpen))
		{
			const float labelWidth = PropertyGridLabelWidth(
				{"LOCPOT defektu", "LOCPOT bulk", "Ładunek q", "ε", "Źródło ε", "Środek", "ENCUT"});
			drawSavedEntries(labelWidth);
			PropertyGrid grid("##FnvData", labelWidth);
			if (grid)
			{
				grid.Row("LOCPOT defektu");
				if (DrawPathField("defect", m_Request.defectLocpot, m_Error))
				{
					if (!selectSavedLocpot(m_Request.defectLocpot))
					{
						if (const auto charge = ChargeFromCalculationFolder(m_Request.defectLocpot))
							m_Request.charge = *charge;
						if (m_Request.bulkLocpot.Empty())
							if (const auto bulk = FindBulkLocpotNear(m_Request.defectLocpot))
								m_Request.bulkLocpot = *bulk;
						proposeCentre();
						invalidateProfiles();
					}
				}
				grid.Row("LOCPOT bulk");
				if (DrawPathField("bulk", m_Request.bulkLocpot, m_Error))
				{
					proposeCentre();
					invalidateProfiles();
				}
				grid.Row("Ładunek q", "q = -1: dodatkowy elektron. sxdefectalign: --charge = -q");
				if (ImGui::InputInt("##q", &m_Request.charge))
					invalidateProfiles();
				ImGui::SetItemTooltip("q = -1: dodatkowy elektron. sxdefectalign: --charge = -q");
				grid.Row("ε");
				if (ImGui::InputDouble("##epsilon", &m_Request.dielectric, 0.0, 0.0, "%.3f"))
					invalidateProfiles();
				grid.Row("Źródło ε");
				if (ImGui::InputText("##epsilonSource", m_DielectricSource.data(), m_DielectricSource.size()))
					m_Dirty = true;
				grid.Row("Środek", "Współrzędne ułamkowe środka defektu.");
				if (ImGui::InputScalarN("##centre", ImGuiDataType_Double, &m_Request.centreFractional.x, 3,
					nullptr, nullptr, "%.6f"))
				{
					m_CentreSource = "ręcznie";
					invalidateProfiles();
				}
				std::optional<glm::dvec3> densityCentre, atomsCentre;
				for (const auto &window : m_RendererLayer.GetWindows())
				{
					if (window.windowId != m_RendererLayer.GetLastFocusedViewportWindowId())
						continue;
					if (!window.selectedSceneDensities.empty())
					{
						const auto density = std::find_if(window.sceneDensities.begin(), window.sceneDensities.end(),
							[&](const auto &candidate) { return candidate.id == window.selectedSceneDensities.front(); });
						if (density != window.sceneDensities.end())
							if (const auto centre = SceneDensityAnalysisCentreFractional(*density))
								densityCentre = glm::dvec3(*centre);
					}
					if (const auto centre = SelectedAtomsCentroid(window))
						atomsCentre = FractionalCoordinates(window.structure.lattice, *centre);
					break;
				}
				grid.Row("");
				ImGui::BeginDisabled(!densityCentre);
				if (ImGui::Button("Z gęstości"))
				{
					m_Request.centreFractional = *densityCentre;
					m_CentreSource = "centroid gęstości";
					invalidateProfiles();
				}
				ImGui::EndDisabled();
				ImGui::SameLine();
				ImGui::BeginDisabled(!atomsCentre);
				if (ImGui::Button("Z zaznaczonych atomów"))
				{
					m_Request.centreFractional = *atomsCentre;
					m_CentreSource = "zaznaczone atomy";
					invalidateProfiles();
				}
				ImGui::EndDisabled();
				ImGui::SameLine();
				ImGui::BeginDisabled(m_Request.defectLocpot.Empty() || m_Request.bulkLocpot.Empty());
				if (ImGui::Button("Z domieszek"))
				{
					proposeCentre();
					invalidateProfiles();
				}
				ImGui::SetItemTooltip("Centroid atomów, których nie ma w bulku (CONTCAR obok LOCPOT) - jak w pracy inż.");
				ImGui::EndDisabled();
				ImGui::SameLine();
				ImGui::BeginDisabled(m_Request.defectLocpot.Empty());
				if (ImGui::Button("Znacznik w scenie"))
					syncChargeMarker(true);
				ImGui::SetItemTooltip("Znacznik ładunku w ostatnim aktywnym widoku, związany z polem Środek: "
					"przesunięcie znacznika zmienia środek i odwrotnie.");
				ImGui::EndDisabled();
				if (!m_CentreSource.empty())
				{
					grid.Row("");
					if (m_CentreSource.starts_with("brak"))
						ImGui::TextColored(kWarningColor, "%s", m_CentreSource.c_str());
					else
						ImGui::TextDisabled("źródło: %s", m_CentreSource.c_str());
				}
				grid.Row("ENCUT", "0 = z INCAR obok LOCPOT defektu (bez INCAR: 520 eV).");
				if (ImGui::InputDouble("##encut", &m_Request.energyCutoff, 0.0, 0.0,
					m_Request.energyCutoff == 0.0 ? "z INCAR" : "%.1f eV"))
					invalidateProfiles();
			}
		}
		const auto &centre = m_Request.centreFractional;
		const bool valid = !m_Request.defectLocpot.Empty() && !m_Request.bulkLocpot.Empty() && m_Request.charge != 0 &&
			std::isfinite(m_Request.dielectric) && m_Request.dielectric > 0.0 &&
			std::isfinite(centre.x) && std::isfinite(centre.y) && std::isfinite(centre.z) &&
			std::isfinite(m_Request.energyCutoff) && m_Request.energyCutoff >= 0.0;
		ImGui::BeginDisabled(m_PendingJob != nullptr || !valid);
		if (ImGui::Button("Oblicz profile (krok 1)", ImVec2(-1.0f, 0.0f)))
			submitProfiles();
		ImGui::EndDisabled();
		if (m_PendingJob)
			ImGui::TextDisabled("Liczenie... (odczyt LOCPOT ~1 min)");
		if (!m_Error.empty())
		{
			ImGui::PushStyleColor(ImGuiCol_Text, kWarningColor);
			ImGui::TextWrapped("%s", m_Error.c_str());
			ImGui::PopStyleColor();
		}
	}

	void FnvCorrectionPanel::drawResult()
	{
		if (!ImGui::CollapsingHeader("Wynik", ImGuiTreeNodeFlags_DefaultOpen))
			return;
		const float labelWidth = PropertyGridLabelWidth({"Konwencja G=0", "E_lat", "C", "E_corr", "Δ (E_corr−E_lat)"});
		{
			PropertyGrid grid("##FnvConvention", labelWidth);
			if (grid)
			{
				grid.Row("Konwencja G=0");
				int convention = m_Convention == FnvModelConvention::Sxdefectalign ? 0 : 1;
				if (ImGui::Combo("##convention", &convention, "sxdefectalign (praca inż.)\0pymatgen\0"))
				{
					m_Convention = convention == 0 ? FnvModelConvention::Sxdefectalign : FnvModelConvention::Pymatgen;
					proposeWindows();
				}
			}
		}
		if (!m_Profiles)
			return;
		if (ImGui::BeginTable("##FnvAxes", 7, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
			ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoSavedSettings))
		{
			for (const char *label : {"Oś", "Użyj", "Okno [Å]", "C [eV]", "Rozrzut", "Nachylenie", "Plateau"})
				ImGui::TableSetupColumn(label);
			ImGui::TableHeadersRow();
			for (int a = 0; a < 3; ++a)
			{
				ImGui::PushID(a);
				const auto &alignment = m_Alignments[a];
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(m_AxisLabels[a].c_str());
				ImGui::TableSetColumnIndex(1);
				if (ImGui::Checkbox("##use", &m_UseAxis[a]))
					m_Dirty = true;
				ImGui::TableSetColumnIndex(2); ImGui::Text("%.2f ± %.2f", alignment.window.centre, alignment.window.width / 2.0);
				ImGui::TableSetColumnIndex(3); ImGui::Text("%+.4f", alignment.alignment);
				ImGui::TableSetColumnIndex(4); ImGui::Text("%.4f", alignment.spread);
				ImGui::TableSetColumnIndex(5); ImGui::Text("%.2f", alignment.slope);
				ImGui::TableSetColumnIndex(6); ImGui::TextUnformatted(alignment.flat ? "tak" : "nie");
				ImGui::PopID();
			}
			ImGui::EndTable();
		}
		const auto correction = CombineFnvCorrection(*m_Profiles, m_Alignments, m_UseAxis);
		{
			PropertyGrid grid("##FnvResult", labelWidth);
			if (grid)
			{
				char text[128];
				std::snprintf(text, sizeof(text), "%.4f eV", correction.latticeEnergy); grid.Value("E_lat", text);
				std::snprintf(text, sizeof(text), "%+.4f ± %.4f eV", correction.alignment, correction.alignmentSpread / 2.0);
				grid.Value("C", text);
				std::snprintf(text, sizeof(text), "%.4f eV (= E_lat + q C)", correction.correction); grid.Value("E_corr", text);
				std::snprintf(text, sizeof(text), "%+.4f eV", correction.correction - correction.latticeEnergy);
				grid.Value("Δ (E_corr−E_lat)", text);
			}
		}
		const bool useAll = std::none_of(m_UseAxis.begin(), m_UseAxis.end(), [](bool use) { return use; });
		double slope = 0.0;
		int count = 0;
		ImGui::PushStyleColor(ImGuiCol_Text, kWarningColor);
		for (int a = 0; a < 3; ++a)
		{
			if (!useAll && !m_UseAxis[a])
				continue;
			slope += m_Alignments[a].slope;
			++count;
			if (!m_Alignments[a].flat)
				ImGui::TextWrapped("Oś %s: brak plateau - przesuń okno albo zmień oś.", m_AxisLabels[a].c_str());
		}
		slope /= count;
		if (slope < 0.3)
			ImGui::TextWrapped("Nachylenie ΔV/V_model = %.2f: ładunek zdelokalizowany - FNV nie ma sensu.", slope);
		else if (slope < 0.7)
			ImGui::TextWrapped("Nachylenie ΔV/V_model = %.2f: ładunek częściowo zdelokalizowany, poprawka FNV przeszacowana.", slope);
		ImGui::PopStyleColor();
		if (useAll)
			ImGui::TextDisabled("Nie wybrano osi: używane są wszystkie trzy.");
		if (ImGui::Button("Zaproponuj okna"))
			proposeWindows();
		ImGui::SameLine();
		if (m_Profiles && ImGui::Button("Kopiuj wynik"))
			copyResult();
	}

	void FnvCorrectionPanel::copyResult() const
	{
		const auto correction = CombineFnvCorrection(*m_Profiles, m_Alignments, m_UseAxis);
		const auto cleanTsv = [](std::string value) {
			std::replace_if(value.begin(), value.end(), [](char c) { return c == '\t' || c == '\r' || c == '\n'; }, ' ');
			return value;
		};
		const auto &centre = m_Profiles->centreFractional;
		const std::string line = std::format("{}\t{}\t{:.3f}\t{}\t{:.6f},{:.6f},{:.6f}\t{:.4f}\t{:+.4f}\t{:.4f}\t{}",
			cleanTsv(m_Request.defectLocpot.parent_path().Utf8()), m_Profiles->charge, m_Profiles->dielectric,
			cleanTsv(m_DielectricSource.data()), centre.x, centre.y, centre.z, correction.latticeEnergy,
			correction.alignment, correction.correction, m_Convention == FnvModelConvention::Sxdefectalign ? "sxdefectalign" : "pymatgen");
		ImGui::SetClipboardText(line.c_str());
	}

	void FnvCorrectionPanel::syncChargeMarker(const bool create)
	{
		const std::string key = m_Request.defectLocpot.Utf8();
		if (key != m_MarkerKey)
			m_MarkerCentre.reset(); // another defect: its marker follows the centre, not the other way
		m_MarkerKey = key;
		if (key.empty())
			return;
		for (auto &window : m_RendererLayer.GetWindows())
		{
			if (window.windowId != m_RendererLayer.GetLastFocusedViewportWindowId())
				continue;
			const glm::dmat3 lattice(window.structure.lattice);
			auto marker = std::find_if(window.freeLabels.begin(), window.freeLabels.end(),
				[&](const auto &label) { return label.charge && label.fnvKey == key; });
			if (marker == window.freeLabels.end())
			{
				if (!create)
					return;
				PushPinnedMeasurementUndoSnapshot(window);
				RendererWindowState::FreeLabel label;
				label.id = window.sceneRegistry.AllocateObjectId();
				label.fnvKey = key;
				window.freeLabels.push_back(std::move(label));
				SceneSystem::SyncLabelEntities(window.sceneRegistry, window);
				marker = std::prev(window.freeLabels.end());
				marker->worldPosition = glm::vec3(lattice * m_Request.centreFractional);
			}
			const auto at = FractionalCoordinates(window.structure.lattice, marker->worldPosition);
			if (!at)
				return;
			const auto differs = [](const glm::dvec3 &a, const glm::dvec3 &b) {
				const glm::dvec3 d = a - b;
				return glm::length(d - glm::round(d)) > 1e-5; // float positions: ~1e-7 of a cell
			};
			if (m_MarkerCentre && differs(*at, *m_MarkerCentre))
			{
				// Moved in the scene (G, the gizmo, a drag): the scene wins.
				m_Request.centreFractional = *at;
				m_CentreSource = "znacznik w scenie";
				invalidateProfiles();
			}
			else if (differs(*at, m_Request.centreFractional))
			{
				marker->worldPosition = glm::vec3(lattice * m_Request.centreFractional);
				if (const auto anchor = ResolveFreeLabelAnchor(window, *marker))
					marker->anchorOffset = marker->worldPosition - *anchor;
			}
			if (marker->charge != m_Request.charge)
			{
				if (!marker->charge || marker->text == ChargeMarkerText(*marker->charge))
					marker->text = ChargeMarkerText(m_Request.charge);
				marker->charge = m_Request.charge;
			}
			m_MarkerCentre = FractionalCoordinates(window.structure.lattice, marker->worldPosition);
			return;
		}
	}

	void FnvCorrectionPanel::Render()
	{
		pollJob();
		if (!IsVisible())
		{
			persistChanges();
			return;
		}
		ImGui::SetNextWindowSize(ImVec2(780.0f, 760.0f), ImGuiCond_FirstUseEver);
		bool stillOpen = true;
		const bool draw = ImGui::Begin(GetTitle().c_str(), &stillOpen);
		if (!stillOpen)
			SetVisible(false);
		if (draw)
		{
			syncChargeMarker(false);
			drawData();
			if (m_Profiles)
				drawPlots();
			if (m_Profiles)
				drawResult();
		}
		ImGui::End();
		persistChanges();
	}
} // namespace DefectStudio
