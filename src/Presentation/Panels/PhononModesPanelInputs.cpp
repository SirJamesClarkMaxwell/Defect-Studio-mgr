#include "Core/dspch.hpp"
#include "Presentation/Panels/PhononModesPanel.hpp"

#include <algorithm>
#include <cmath>
#include <imgui.h>

#include "Core/JobSystem/JobSystem.hpp"
#include "Core/Platform/FileDialog.hpp"
#include "Presentation/Panels/PropertyGrid.hpp"

namespace DefectStudio
{
	namespace
	{
		bool DrawFolder(const char *id, Path &path, std::string &error, bool editable = false)
		{
			ImGui::PushID(id);
			const auto full = path.Utf8();
			std::vector<char> text(full.begin(), full.end());
			text.resize(full.size() + 1024, '\0');
			ImGui::SetNextItemWidth(std::max(1.0f, ImGui::GetContentRegionAvail().x -
				ImGui::GetFrameHeight() - ImGui::GetStyle().ItemSpacing.x));
			if (ImGui::InputText("##directory", text.data(), text.size(), editable ? 0 : ImGuiInputTextFlags_ReadOnly))
				path = Path::FromUtf8(text.data());
			bool changed = editable && ImGui::IsItemDeactivatedAfterEdit();
			ImGui::SetItemTooltip("%s", full.c_str());
			ImGui::SameLine();
			if (ImGui::Button("..."))
			{
				const auto picked = Platform::PickFolder(path);
				if (!picked) error = picked.Error().userMessage;
				else if (picked->has_value())
				{
					path = **picked;
					changed = true;
				}
			}
			ImGui::PopID();
			return changed;
		}
	}

	void PhononModesPanel::drawInputs()
	{
		auto &settings = m_Session->Settings();
		ImGui::SetNextItemOpen(!m_Session->Result(), ImGuiCond_Once);
		if (ImGui::CollapsingHeader("Wejścia"))
		{
			ImGui::TextDisabled("Katalogi można też ustawić przez RMB → Fonony w Project Tree.");
			ImGui::BeginDisabled(m_PendingJob != nullptr);
			{
				PropertyGrid grid("##phononInputs", PropertyGridLabelWidth({"Stałe siłowe (FC)",
					"Stan podstawowy", "Stan wzbudzony", "Tryplet (opcjonalny)", "ZPL eksperymentalne [eV]", "Katalog wyników"}));
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
					grid.Row("ZPL eksperymentalne [eV]");
					bool experimental = settings.experimentalZplEv.has_value();
					if (ImGui::Checkbox("##useZpl", &experimental))
					{
						settings.experimentalZplEv = experimental ? std::optional<double>(2.0) : std::nullopt;
						persist();
					}
					if (experimental)
					{
						ImGui::SameLine();
						ImGui::InputDouble("##zpl", &*settings.experimentalZplEv, 0.0, 0.0, "%.4f");
						if (ImGui::IsItemDeactivatedAfterEdit())
						{
							if (!std::isfinite(*settings.experimentalZplEv) || *settings.experimentalZplEv <= 0.0)
								settings.experimentalZplEv = 2.0;
							persist();
						}
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
					if (DrawFolder("output", settings.resultDirectory, m_Error, true)) persist();
				}
			}
			ImGui::EndDisabled();
		}
		ImGui::BeginDisabled(m_PendingJob != nullptr || settings.forceConstantDirectory.Empty() ||
			settings.groundStateDirectory.Empty() || settings.excitedStateDirectory.Empty() || settings.resultDirectory.Empty());
		if (ImGui::Button("Policz")) submitComputation();
		ImGui::EndDisabled();
		ImGui::SameLine();
		ImGui::BeginDisabled(m_PendingJob != nullptr);
		if (ImGui::Button("Wczytaj wyniki..."))
		{
			const auto picked = Platform::PickFolder(settings.resultDirectory);
			if (!picked) m_Error = picked.Error().userMessage;
			else if (picked->has_value()) SetDirectory("result", **picked);
		}
		ImGui::EndDisabled();
		if (m_PendingJob)
		{
			const auto jobs = m_JobSystem.lock();
			const auto snapshot = jobs ? jobs->GetJob(m_PendingJobId) : std::nullopt;
			if (snapshot)
				ImGui::ProgressBar(snapshot->totalWork > 0.0f ? snapshot->completedWork / snapshot->totalWork : 0.0f,
					ImVec2(-1.0f, 0.0f), snapshot->currentMessage.c_str());
			if (ImGui::Button("Anuluj") && jobs) (void)jobs->RequestCancel(m_PendingJobId);
		}
		if (!m_Error.empty())
			ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "%s", m_Error.c_str());
	}
}
