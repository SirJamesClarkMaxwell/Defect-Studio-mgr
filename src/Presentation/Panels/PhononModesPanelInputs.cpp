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
	namespace
	{
		// Why "Policz" is disabled, in the user's words; empty when it can run.
		std::string ComputationBlocker(const PhononPanelSettings &settings, const Path &output)
		{
			if (settings.forceConstantDirectory.Empty()) return "Ustaw katalog stałych siłowych (FC).";
			if (settings.groundStateDirectory.Empty()) return "Ustaw stan podstawowy.";
			if (settings.excitedStateDirectory.Empty()) return "Ustaw stan wzbudzony.";
			if (output.Empty()) return "Ustaw katalog projektu lub katalog wyników.";
			if (!settings.embeddingMult.empty() && settings.bulkForceConstantDirectory.Empty())
				return "Embedding włączony bez katalogu FC bulku - ustaw go albo odznacz Embedding.";
			for (const Path &input : {settings.forceConstantDirectory, settings.groundStateDirectory,
				settings.excitedStateDirectory, settings.tripletDirectory, settings.groundAtExcitedDirectory,
				settings.excitedAtGroundDirectory, settings.embeddingMult.empty() ? Path{} : settings.bulkForceConstantDirectory})
				if (PhononPanelSession::OutputContainsInput(output, input))
					return "Katalog wyników nie może być katalogiem wejściowym ani go zawierać.";
			return {};
		}
	}

	bool PhononModesPanel::DrawFolder(const char *id, Path &path, std::string &error, bool editable, const Path &hint)
	{
		ImGui::PushID(id);
		auto &[text, editing] = m_FolderEdits[id];
		if (!editing)
		{
			const auto label = PhononPanelSession::DirectoryLabel(path, m_ProjectDirectory, m_ProjectRoots);
			text.assign(label.begin(), label.end());
			text.resize(label.size() + 1024, '\0');
		}
		ImGui::SetNextItemWidth(std::max(1.0f, ImGui::GetContentRegionAvail().x -
			ImGui::GetFrameHeight() - ImGui::GetStyle().ItemSpacing.x));
		const auto hintLabel = PhononPanelSession::DirectoryLabel(hint, m_ProjectDirectory, m_ProjectRoots);
		ImGui::InputTextWithHint("##directory", hintLabel.c_str(), text.data(), text.size(), editable ? 0 : ImGuiInputTextFlags_ReadOnly);
		editing = ImGui::IsItemActive();
		bool changed = editable && ImGui::IsItemDeactivatedAfterEdit();
		if (changed) path = PhononPanelSession::ResolveDirectoryText(text.data(), m_ProjectDirectory);
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
			ImGui::BeginDisabled(m_PendingJob != nullptr || m_PendingCcdJob != nullptr || m_PendingDispersionJob != nullptr || m_PendingCcdAnalysisJob != nullptr);
			{
				PropertyGrid grid("##phononInputs", PropertyGridLabelWidth({"Stałe siłowe (FC)",
					"Stan podstawowy", "Stan wzbudzony", "Tryplet (opcjonalny)", "Katalog wyników"}));
				if (grid)
				{
					grid.Row("Stałe siłowe (FC)");
					if (DrawFolder("fc", settings.forceConstantDirectory, m_Error)) SetDirectory("fc", settings.forceConstantDirectory);
					grid.Row("Stan podstawowy");
					if (DrawFolder("gs", settings.groundStateDirectory, m_Error)) persist();
					grid.Row("Stan wzbudzony");
					if (DrawFolder("es", settings.excitedStateDirectory, m_Error)) persist();
					grid.Row("Tryplet (opcjonalny)");
					if (DrawFolder("triplet", settings.tripletDirectory, m_Error)) persist();
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
					if (DrawFolder("output", settings.outputDirectory, m_Error, true, computationOutputPath())) persist();
				}
			}
			drawZplInputs();
			ImGui::SeparatorText("Embedding (opcjonalnie)");
			drawEmbedding();
			ImGui::EndDisabled();
		}
		const std::string blocker = ComputationBlocker(settings, computationOutputPath());
		ImGui::BeginDisabled(m_PendingJob != nullptr || m_PendingCcdJob != nullptr || m_PendingDispersionJob != nullptr || m_PendingCcdAnalysisJob != nullptr || !blocker.empty());
		if (ImGui::Button("Policz")) submitComputation();
		ImGui::EndDisabled();
		if (!blocker.empty()) ImGui::SetItemTooltip("%s", blocker.c_str());
		else if (m_PendingJob || m_PendingCcdJob || m_PendingDispersionJob || m_PendingCcdAnalysisJob) ImGui::SetItemTooltip("Poczekaj na zakończenie bieżącego zadania.");
		ImGui::SameLine();
		ImGui::BeginDisabled(m_PendingJob != nullptr || m_PendingCcdJob != nullptr || m_PendingDispersionJob != nullptr || m_PendingCcdAnalysisJob != nullptr);
		if (ImGui::Button("Wczytaj wyniki..."))
		{
			const auto picked = Platform::PickFolder(settings.resultDirectory);
			if (!picked) m_Error = picked.Error().userMessage;
			else if (picked->has_value()) SetDirectory("result", **picked);
		}
		ImGui::EndDisabled();
		if (m_PendingJob || m_PendingCcdJob || m_PendingDispersionJob || m_PendingCcdAnalysisJob) ImGui::SetItemTooltip("Poczekaj na zakończenie bieżącego zadania.");
		if (m_PendingJob)
		{
			const auto jobs = m_JobSystem.lock();
			const auto snapshot = jobs ? jobs->GetJob(m_PendingJobId) : std::nullopt;
			if (snapshot)
				ImGui::ProgressBar(snapshot->totalWork > 0.0f ? snapshot->completedWork / snapshot->totalWork : 0.0f,
					ImVec2(-1.0f, 0.0f), snapshot->currentMessage.c_str());
			if (ImGui::Button("Anuluj") && jobs) (void)jobs->RequestCancel(m_PendingJobId);
		}
		if (!blocker.empty() && !m_PendingJob)
			ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f), "%s", blocker.c_str());
		if (!m_Error.empty())
			ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "%s", m_Error.c_str());
	}

	void PhononModesPanel::drawEmbedding()
	{
		auto &settings = m_Session->Settings();
		const auto storeMult = [&] {
			for (auto &value : m_EmbeddingMult) value = std::max(1, value);
			settings.embeddingMult = std::format("{}x{}x{}", m_EmbeddingMult[0], m_EmbeddingMult[1], m_EmbeddingMult[2]);
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
				if (DrawFolder("bulk", settings.bulkForceConstantDirectory, m_Error, true)) persist();
				grid.Row("mult (N x M x K)");
				ImGui::InputInt3("##mult", m_EmbeddingMult.data());
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
