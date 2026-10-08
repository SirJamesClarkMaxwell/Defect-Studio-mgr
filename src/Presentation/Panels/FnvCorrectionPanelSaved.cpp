#include "Core/dspch.hpp"

#include "Presentation/Panels/FnvCorrectionPanel.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <format>

#include <imgui.h>

#include "Presentation/Panels/PropertyGrid.hpp"

namespace DefectStudio
{
	namespace
	{
		std::string EntryLabel(const PersistedFnvCorrection &entry)
		{
			const auto folder = entry.defectLocpot.parent_path().Native().lexically_normal();
			const auto relative = folder.lexically_relative(folder.parent_path().parent_path());
			const auto utf8 = (relative.empty() ? folder : relative).generic_u8string();
			return std::format("{}  q={}", std::string(utf8.begin(), utf8.end()), entry.charge);
		}
	} // namespace

	void FnvCorrectionPanel::SetPersistedState(FnvCorrectionsFile file)
	{
		*m_Saved = std::move(file);
		m_SavedLabels.clear();
		loadSavedEntry();
	}

	void FnvCorrectionPanel::loadSavedEntry()
	{
		++m_InputRevision; // A job for the previous inputs may finish, but cannot replace this state.
		m_Dirty = false;
		m_Error.clear();
		m_Request = {};
		m_Request.dielectric = 5.7;
		m_DielectricSource.fill('\0');
		std::snprintf(m_DielectricSource.data(), m_DielectricSource.size(), "%s", "promotor (diament)");
		m_Convention = FnvModelConvention::Sxdefectalign;
		m_UseAxis = {true, true, true};
		m_Profiles.reset();
		m_Alignments = {};
		m_ModelPotential = {};
		if (m_Saved->active < 0 || m_Saved->active >= static_cast<int>(m_Saved->corrections.size()))
		{
			m_Saved->active = -1;
			return;
		}
		const auto &entry = m_Saved->corrections[m_Saved->active];
		m_Request = {entry.defectLocpot, entry.bulkLocpot, entry.charge, entry.dielectric,
			entry.centreFractional, entry.energyCutoff};
		std::snprintf(m_DielectricSource.data(), m_DielectricSource.size(), "%s", entry.dielectricSource.c_str());
		m_Convention = entry.convention;
		m_UseAxis = entry.useAxis;
		m_Profiles = entry.profiles;
		rebuildAlignments(entry.windows);
	}

	PersistedFnvCorrection FnvCorrectionPanel::currentEntry() const
	{
		PersistedFnvCorrection entry;
		entry.defectLocpot = m_Request.defectLocpot;
		entry.bulkLocpot = m_Request.bulkLocpot;
		entry.charge = m_Request.charge;
		entry.dielectric = m_Request.dielectric;
		entry.dielectricSource = m_DielectricSource.data();
		entry.centreFractional = m_Request.centreFractional;
		entry.energyCutoff = m_Request.energyCutoff;
		entry.convention = m_Convention;
		entry.useAxis = m_UseAxis;
		entry.profiles = m_Profiles;
		if (m_Profiles)
			for (int a = 0; a < 3; ++a)
				entry.windows[a] = m_Alignments[a].window;
		return entry;
	}

	void FnvCorrectionPanel::publishSavedState()
	{
		if (m_SaveHandler)
			m_SaveHandler(*m_Saved);
	}

	void FnvCorrectionPanel::persistChanges()
	{
		if (!m_Dirty || ImGui::IsMouseDown(ImGuiMouseButton_Left) || m_Request.defectLocpot.Empty())
			return;
		UpsertFnvCorrection(*m_Saved, currentEntry());
		m_Dirty = false;
		m_SavedLabels.clear();
		publishSavedState();
	}

	bool FnvCorrectionPanel::selectSavedLocpot(const Path &path)
	{
		const auto found = std::find_if(m_Saved->corrections.begin(), m_Saved->corrections.end(), [&](const auto &entry) {
			return entry.defectLocpot.Native().lexically_normal() == path.Native().lexically_normal();
		});
		if (found == m_Saved->corrections.end())
			return false;
		m_Saved->active = static_cast<int>(found - m_Saved->corrections.begin());
		loadSavedEntry();
		publishSavedState();
		return true;
	}

	void FnvCorrectionPanel::drawSavedEntries(const float labelWidth)
	{
		if (m_Saved->corrections.empty())
			return;
		PropertyGrid grid("##FnvSavedData", labelWidth);
		if (!grid)
			return;
		grid.Row("Zapisane");
		const float buttonWidth = ImGui::CalcTextSize("Usuń").x + 2.0f * ImGui::GetStyle().FramePadding.x;
		ImGui::SetNextItemWidth(std::max(1.0f, ImGui::GetContentRegionAvail().x - buttonWidth - ImGui::GetStyle().ItemSpacing.x));
		const std::string preview = m_Saved->active < 0 ? "Nowe" :
			(m_SavedLabels.size() == m_Saved->corrections.size() ? m_SavedLabels[m_Saved->active] :
				EntryLabel(m_Saved->corrections[m_Saved->active]));
		std::optional<int> selected;
		if (ImGui::BeginCombo("##FnvSaved", preview.c_str()))
		{
			if (ImGui::IsWindowAppearing() || m_SavedLabels.size() != m_Saved->corrections.size())
			{
				m_SavedLabels.clear();
				for (const auto &entry : m_Saved->corrections)
				{
					std::string energy = "—";
					if (entry.profiles)
					{
						try
						{
							std::array<FnvAxisAlignment, 3> alignments;
							for (int a = 0; a < 3; ++a)
								alignments[a] = AlignFnvInWindow(entry.profiles->axes[a],
									FnvModelPotential(*entry.profiles, a, entry.convention), entry.windows[a]);
							const double correction = CombineFnvCorrection(*entry.profiles, alignments, entry.useAxis).correction;
							if (std::isfinite(correction))
								energy = std::format("{:.4f}", correction);
						}
						catch (const std::exception &)
						{
							// Invalid saved profiles have no result to display.
						}
					}
					m_SavedLabels.push_back(EntryLabel(entry) + "  E_corr=" + energy);
				}
			}
			if (ImGui::Selectable("Nowe", m_Saved->active == -1))
				selected = -1;
			for (int i = 0; i < static_cast<int>(m_Saved->corrections.size()); ++i)
			{
				ImGui::PushID(i);
				if (ImGui::Selectable(m_SavedLabels[i].c_str(), m_Saved->active == i))
					selected = i;
				ImGui::PopID();
			}
			ImGui::EndCombo();
		}
		if (selected)
		{
			m_Saved->active = *selected;
			loadSavedEntry();
			publishSavedState();
		}
		ImGui::SameLine();
		ImGui::BeginDisabled(m_Saved->active < 0);
		if (ImGui::Button("Usuń"))
		{
			m_Saved->corrections.erase(m_Saved->corrections.begin() + m_Saved->active);
			m_Saved->active = -1;
			m_SavedLabels.clear();
			loadSavedEntry();
			publishSavedState();
		}
		ImGui::EndDisabled();
	}
} // namespace DefectStudio
