#include "Core/dspch.hpp"
#include "Presentation/Panels/PhononModesPanel.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <imgui.h>

#include "Core/JobSystem/JobSystem.hpp"
#include "Core/Platform/FileDialog.hpp"
#include "Presentation/Panels/PropertyGrid.hpp"

namespace DefectStudio
{
	bool PhononModesPanel::DrawFolder(const char *id, Path &path, std::string &error, bool editable, const Path &hint)
	{
		ImGui::PushID(id);
		auto &[text, editing] = m_FolderEdits[id];
		if (!editing)
		{
			const auto label = PhononPanelSession::DirectoryLabel(path, m_Session->m_ProjectDirectory, m_Session->m_ProjectRoots);
			text.assign(label.begin(), label.end());
			text.resize(label.size() + 1024, '\0');
		}
		ImGui::SetNextItemWidth(std::max(1.0f, ImGui::GetContentRegionAvail().x -
			ImGui::GetFrameHeight() - ImGui::GetStyle().ItemSpacing.x));
		const auto hintLabel = PhononPanelSession::DirectoryLabel(hint, m_Session->m_ProjectDirectory, m_Session->m_ProjectRoots);
		ImGui::InputTextWithHint("##directory", hintLabel.c_str(), text.data(), text.size(), editable ? 0 : ImGuiInputTextFlags_ReadOnly);
		editing = ImGui::IsItemActive();
		bool changed = editable && ImGui::IsItemDeactivatedAfterEdit();
		if (changed) path = PhononPanelSession::ResolveDirectoryText(text.data(), m_Session->m_ProjectDirectory);
		ImGui::SetItemTooltip("%s", (path.Empty() ? hint : path).Utf8().c_str());
		ImGui::SameLine();
		if (ImGui::Button("..."))
		{
			const auto picked = Platform::PickFolder(path.Empty() ? hint : path);
			if (!picked) error = picked.Error().userMessage;
			else if (picked->has_value()) { path = **picked; changed = true; }
		}
		ImGui::PopID();
		return changed;
	}

	void PhononModesPanel::drawInputs()
	{
		auto &settings = m_Session->Settings();
		ImGui::SetNextItemOpen(!m_Session->Result(), ImGuiCond_Once);
		if (ImGui::CollapsingHeader("Wejścia"))
		{
			ImGui::TextDisabled("Katalogi można też ustawić przez RMB → Fonony w Project Tree.");
			ImGui::BeginDisabled(m_Session->IsBusy() && !m_Session->IsBatchRunning());
			{
				PropertyGrid grid("##phononInputs", PropertyGridLabelWidth({"Stałe siłowe (FC)",
					"Stan podstawowy", "Stan wzbudzony", "Tryplet (opcjonalny)", "Katalog wyników"}));
				if (grid)
				{
					grid.Row("Stałe siłowe (FC)");
					if (DrawFolder("fc", settings.forceConstantDirectory, m_Session->m_Error)) SetDirectory("fc", settings.forceConstantDirectory);
					grid.Row("Stan podstawowy");
					if (DrawFolder("gs", settings.groundStateDirectory, m_Session->m_Error)) SetDirectory("gs", settings.groundStateDirectory);
					grid.Row("Stan wzbudzony");
					if (DrawFolder("es", settings.excitedStateDirectory, m_Session->m_Error)) SetDirectory("es", settings.excitedStateDirectory);
					grid.Row("Tryplet (opcjonalny)");
					if (DrawFolder("triplet", settings.tripletDirectory, m_Session->m_Error)) SetDirectory("triplet", settings.tripletDirectory);
					if (!settings.tripletDirectory.Empty())
					{
						grid.Row("");
						if (ImGui::SmallButton("Usuń tryplet")) { settings.tripletDirectory = Path{}; persist(); }
					}
					grid.Row("sigma [meV]");
					ImGui::InputDouble("##sigma", &settings.sigmaMeV, 0.0, 0.0, "%.3f");
					if (ImGui::IsItemDeactivatedAfterEdit())
					{
						if (!std::isfinite(settings.sigmaMeV) || settings.sigmaMeV <= 0.0) settings.sigmaMeV = 6.0;
						persist();
					}
					grid.Row("gamma [meV]");
					ImGui::InputDouble("##gamma", &settings.gammaMeV, 0.0, 0.0, "%.3f");
					if (ImGui::IsItemDeactivatedAfterEdit())
					{
						if (!std::isfinite(settings.gammaMeV) || settings.gammaMeV <= 0.0) settings.gammaMeV = 2.0;
						persist();
					}
					grid.Row("Katalog wyników");
					if (DrawFolder("output", settings.outputDirectory, m_Session->m_Error, true, m_Session->computationOutputPath())) persist();
				}
			}
			drawZplInputs();
			ImGui::SeparatorText("Embedding (opcjonalnie)");
			drawEmbedding();
			ImGui::EndDisabled();
		}
		const std::string blocker = m_Session->ComputationBlocker();
		ImGui::BeginDisabled(m_Session->IsBusy() || !blocker.empty());
		if (ImGui::Button("Policz")) m_Session->SubmitComputation();
		ImGui::EndDisabled();
		if (!blocker.empty()) ImGui::SetItemTooltip("%s", blocker.c_str());
		else if (m_Session->IsBusy()) ImGui::SetItemTooltip("Poczekaj na zakończenie bieżącego zadania.");
		ImGui::SameLine();
		ImGui::BeginDisabled(m_Session->IsBusy());
		if (ImGui::Button("Wczytaj wyniki..."))
		{
			const auto picked = Platform::PickFolder(settings.resultDirectory);
			if (!picked) m_Session->m_Error = picked.Error().userMessage;
			else if (picked->has_value()) SetDirectory("result", **picked);
		}
		ImGui::EndDisabled();
		if (m_Session->IsBusy()) ImGui::SetItemTooltip("Poczekaj na zakończenie bieżącego zadania.");
		if (m_Session->m_PendingJob)
		{
			const auto jobs = m_Session->m_JobSystem.lock();
			const auto snapshot = jobs ? jobs->GetJob(m_Session->m_PendingJobId) : std::nullopt;
			if (snapshot)
				ImGui::ProgressBar(snapshot->totalWork > 0.0f ? snapshot->completedWork / snapshot->totalWork : 0.0f,
					ImVec2(-1.0f, 0.0f), snapshot->currentMessage.c_str());
			if (ImGui::Button("Anuluj"))
			{
				if (m_Session->IsBatchRunning()) m_Session->CancelBatch();
				else m_Session->cancelJob(m_Session->m_PendingJobId);
			}
		}
		if (!blocker.empty() && !m_Session->m_PendingJob)
			ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f), "%s", blocker.c_str());
		if (!m_Session->m_Error.empty())
			ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "%s", m_Session->m_Error.c_str());
	}

	void PhononModesPanel::drawEmbedding()
	{
		auto &settings = m_Session->Settings();
		const auto storeMult = [&] {
			for (auto &value : m_Session->m_EmbeddingMult) value = std::max(1, value);
			settings.embeddingMult = std::format("{}x{}x{}", m_Session->m_EmbeddingMult[0], m_Session->m_EmbeddingMult[1], m_Session->m_EmbeddingMult[2]);
		};
		bool enabled = !settings.embeddingMult.empty();
		if (ImGui::Checkbox("Embedding", &enabled))
		{
			if (enabled) storeMult();
			else settings.embeddingMult.clear();
			persist();
		}
		ImGui::BeginDisabled(!enabled);
		{
			PropertyGrid grid("##phononEmbedding", PropertyGridLabelWidth({"Katalog FC bulku", "mult (N x M x K)", "dcutoff [Å]", "bcutoff [Å]"}));
			if (grid)
			{
				grid.Row("Katalog FC bulku");
				if (DrawFolder("bulk", settings.bulkForceConstantDirectory, m_Session->m_Error, true)) SetDirectory("bulk", settings.bulkForceConstantDirectory);
				grid.Row("mult (N x M x K)");
				ImGui::InputInt3("##mult", m_Session->m_EmbeddingMult.data());
				if (ImGui::IsItemDeactivatedAfterEdit())
				{
					storeMult();
					persist();
				}
				for (const auto &[label, cutoff] : {std::pair{"dcutoff [Å]", std::ref(settings.dcutoffAngstrom)},
					std::pair{"bcutoff [Å]", std::ref(settings.bcutoffAngstrom)}})
				{
					grid.Row(label);
					ImGui::PushID(label);
					auto &value = cutoff.get();
					bool useDefault = !value.has_value();
					if (ImGui::Checkbox("domyślny", &useDefault))
					{
						value = useDefault ? std::nullopt : std::optional<double>(6.0);
						persist();
					}
					if (value)
					{
						ImGui::SameLine();
						ImGui::InputDouble("##cutoff", &*value, 0.0, 0.0, "%.3f");
						if (ImGui::IsItemDeactivatedAfterEdit())
						{
							if (!std::isfinite(*value) || *value <= 0.0) value = 6.0;
							persist();
						}
					}
					ImGui::PopID();
				}
			}
		}
		ImGui::EndDisabled();
		ImGui::TextDisabled("FC bulku: RMB → Fonony → Ustaw jako katalog FC bulku (embedding).");
		ImGui::TextDisabled("Czas rośnie ~N^3: 24x24x1 ~5 s, 36x36x1 ~20 s, 48x48x1 ~2 min na tym PC");
	}

}
