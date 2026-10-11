#include "Core/dspch.hpp"

#include "Presentation/Panels/CalculationSummaryPanel.hpp"

#include <algorithm>

#include <imgui.h>
#include <nlohmann/json.hpp>

#include "Core/JobSystem/JobSystem.hpp"
#include "Core/Logging/Logger.hpp"
#include "Core/Platform/FileDialog.hpp"
#include "IO/TextFileIO.hpp"
#include "ScientificRuntime/Python/VaspOutputJob.hpp"

namespace DefectStudio
{
	namespace
	{
		[[nodiscard]] Path CachePath()
		{
			return Path::FromResolved(
				FileSystem::CurrentPath() / "install" / "users" / "default" / "config" /
				"calculation_summary_cache.json");
		}

		template <typename T>
		nlohmann::json OptJson(const std::optional<T> &value)
		{
			return value.has_value() ? nlohmann::json(*value) : nlohmann::json(nullptr);
		}

		// Same schema vasp_output_load.py prints (and VaspOutputBridge::ParseVaspOutputJson
		// parses) - so the cache round-trips through that exact same parser, no separate cache
		// format/parser to keep in sync.
		nlohmann::json SerializeVaspOutputData(const VaspOutputData &data)
		{
			nlohmann::json payload;
			payload["path"] = data.path.Utf8();
			if (data.gap.has_value())
				payload["gap"] = {{"bandgap", data.gap->bandgap}, {"homo", data.gap->homo}, {"lumo", data.gap->lumo}};
			else
				payload["gap"] = nullptr;
			// This panel always dispatches with includeOrbitals=false - never anything to cache here.
			payload["orbitals"] = nullptr;
			payload["orbitals_error"] = nullptr;

			const VaspOutputSummaryData &summary = data.summary;
			nlohmann::json summaryJson;
			summaryJson["energy_trend"] = OptJson(summary.energyTrend);
			summaryJson["force_trend"] = summary.forceTrend;
			summaryJson["energy_without_entropy"] = summary.energyWithoutEntropy;
			summaryJson["energy_sigma_zero"] = summary.energySigmaZero;
			summaryJson["finished"] = OptJson(summary.finished);
			summaryJson["reached_accuracy"] = OptJson(summary.reachedAccuracy);
			summaryJson["nsw"] = OptJson(summary.nsw);
			summaryJson["ibrion"] = OptJson(summary.ibrion);
			summaryJson["ediffg"] = OptJson(summary.ediffg);
			summaryJson["outcar_write_time"] = OptJson(summary.outcarWriteTime);
			summaryJson["ks_band_start"] = summary.ksBandStart;
			summaryJson["ks_band_end"] = summary.ksBandEnd;
			summaryJson["ks_default_band_start"] = summary.ksDefaultBandStart;
			summaryJson["ks_default_band_end"] = summary.ksDefaultBandEnd;
			summaryJson["ks_steps"] = nlohmann::json::array();
			for (const auto &step : summary.ksSteps)
			{
				nlohmann::json blocks = nlohmann::json::array();
				for (const auto &block : step.blocks)
					blocks.push_back({{"spin", block.spin}, {"kpoint", block.kpoint}, {"bands", block.bands},
						{"energies", block.energies}, {"occupations", block.occupations}});
				summaryJson["ks_steps"].push_back({{"step", step.step}, {"fermi", OptJson(step.fermi)}, {"blocks", blocks}});
			}
			summaryJson["final_energy"] = OptJson(summary.finalEnergy);
			summaryJson["cpu_time"] = OptJson(summary.cpuTimeSeconds);
			summaryJson["user_time"] = OptJson(summary.userTimeSeconds);
			summaryJson["system_time"] = OptJson(summary.systemTimeSeconds);
			summaryJson["elapsed_time"] = OptJson(summary.elapsedTimeSeconds);
			if (summary.totalDrift.has_value())
				summaryJson["total_drift"] = std::vector<double>(summary.totalDrift->begin(), summary.totalDrift->end());
			else
				summaryJson["total_drift"] = nullptr;
			summaryJson["nelect"] = OptJson(summary.nelect);
			summaryJson["ispin"] = OptJson(summary.ispin);
			summaryJson["pressure"] = OptJson(summary.pressureKilobar);
			if (summary.stressTensorKilobar.has_value())
			{
				std::vector<std::vector<double>> rows;
				for (const std::array<double, 3> &row : *summary.stressTensorKilobar)
					rows.emplace_back(row.begin(), row.end());
				summaryJson["stress_tensor"] = rows;
			}
			else
			{
				summaryJson["stress_tensor"] = nullptr;
			}
			summaryJson["space_group_symbol"] = OptJson(summary.spaceGroupSymbol);
			summaryJson["space_group_number"] = OptJson(summary.spaceGroupNumber);
			summaryJson["point_group_symbol"] = OptJson(summary.pointGroupSymbol);
			summaryJson["point_group_schoenflies"] = OptJson(summary.pointGroupSchoenflies);
			payload["summary"] = summaryJson;

			return payload;
		}

		void SaveCache(const VaspOutputData &data)
		{
			std::string error;
			if (!TextFileIO::Save(CachePath(), SerializeVaspOutputData(data).dump(), error))
				DS_LOG_WARN("CalculationSummaryPanel: failed to persist cache: {}", error);
		}

		// Shows instantly at startup ("serialized cache data, not live data" - a user explicitly
		// asked this NOT auto-dispatch a fresh subprocess load; Reload still does that on demand).
		[[nodiscard]] std::optional<VaspOutputData> LoadCache()
		{
			std::string text;
			std::string error;
			if (!TextFileIO::Load(CachePath(), text, error))
				return std::nullopt;
			Result<VaspOutputData> parsed = ParseVaspOutputJson(text);
			if (!parsed)
				return std::nullopt;
			return *parsed;
		}
	} // namespace

	CalculationSummaryPanel::CalculationSummaryPanel(WeakRef<JobSystem> jobSystem, std::string title, bool visibleByDefault)
		: IPanel(std::move(title), visibleByDefault), m_JobSystem(std::move(jobSystem))
	{
		if (std::optional<VaspOutputData> cached = LoadCache(); cached.has_value())
		{
			m_Directory = cached->path;
			Run run;
			run.directory = m_Directory;
			m_AutoBands = false;
			if (cached->summary.ksBandStart > 0)
			{
				m_BandStart = cached->summary.ksBandStart;
				m_BandEnd = cached->summary.ksBandEnd;
			}
			run.data = std::move(cached);
			m_Chain.push_back(std::move(run));
		}
	}

	Ref<IPanel> CalculationSummaryPanel::Clone() const
	{
		return CreateRef<CalculationSummaryPanel>(*this);
	}

	void CalculationSummaryPanel::OpenDirectory(Path directory)
	{
		if (const auto jobs = m_JobSystem.lock())
			for (const auto &run : m_Chain)
				if (run.job)
					(void)jobs->RequestCancel(run.jobId);
		m_Directory = std::move(directory);
		m_Chain.clear();
		Run run;
		run.directory = m_Directory;
		m_Chain.push_back(std::move(run));
		m_DetailRun = 0;
		m_AutoBands = true;
		m_SortPending = false;
		m_Error.clear();
		dispatchLoad();
		SetVisible(true);
	}

	bool CalculationSummaryPanel::isLoading() const
	{
		return std::ranges::any_of(m_Chain, [](const Run &run) { return run.job != nullptr; });
	}

	void CalculationSummaryPanel::dispatchRun(Run &run, int bandStart, int bandEnd)
	{
		const auto jobs = m_JobSystem.lock();
		if (!jobs)
		{
			run.error = "JobSystem unavailable";
			return;
		}
		run.job = CreateRef<VaspOutputJob>(run.directory, bandStart, bandEnd, false, false,
			0.1f, 1e-3f, true, run.outcarName);
		run.jobId = jobs->Submit(run.job, JobPriority::Normal);
		run.error.clear();
		if (run.jobId == 0)
		{
			run.job.reset();
			run.error = "Could not submit load job";
		}
	}

	void CalculationSummaryPanel::dispatchLoad()
	{
		for (auto &run : m_Chain)
			dispatchRun(run, m_AutoBands ? 0 : m_BandStart, m_AutoBands ? 0 : m_BandEnd);
	}

	void CalculationSummaryPanel::pollJob()
	{
		const auto jobs = m_JobSystem.lock();
		if (!jobs)
			return;
		for (auto &run : m_Chain)
		{
			if (!run.job)
				continue;
			const auto snapshot = jobs->GetJob(run.jobId);
			if (!snapshot || snapshot->status == JobStatus::Queued || snapshot->status == JobStatus::Running)
				continue;
			if (snapshot->status == JobStatus::Completed && run.job->GetResult())
			{
				run.data = *run.job->GetResult();
				run.error.clear();
				if (run.directory == m_Directory && run.outcarName == "OUTCAR")
					SaveCache(*run.data);
			}
			else
			{
				run.error = snapshot->errorMessage.empty() ? "Load failed" : snapshot->errorMessage;
			}
			run.job.reset();
			run.jobId = 0;
		}
		if (isLoading())
			return;
		if (m_SortPending)
		{
			std::stable_sort(m_Chain.begin(), m_Chain.end(), [](const Run &a, const Run &b)
			{
				const auto time = [](const Run &run)
				{
					return run.data ? run.data->summary.outcarWriteTime.value_or(0.0) : 0.0;
				};
				return time(a) < time(b);
			});
			m_DetailRun = static_cast<int>(m_Chain.size()) - 1;
			m_SortPending = false;
		}
		if (m_AutoBands)
		{
			for (auto it = m_Chain.rbegin(); it != m_Chain.rend(); ++it)
			{
				if (!it->data || it->data->summary.ksSteps.empty())
					continue;
				m_BandStart = it->data->summary.ksDefaultBandStart;
				m_BandEnd = it->data->summary.ksDefaultBandEnd;
				// The final run defines a common band window for the entire continuation chain.
				for (auto &run : m_Chain)
					if (run.error.empty() && run.data && !run.data->summary.ksSteps.empty() &&
						(run.data->summary.ksBandStart != m_BandStart || run.data->summary.ksBandEnd != m_BandEnd))
						dispatchRun(run, m_BandStart, m_BandEnd);
				break;
			}
		}
	}

	void CalculationSummaryPanel::renderChain()
	{
		const bool loading = isLoading();
		ImGui::BeginDisabled(loading);
		if (ImGui::Button("Reload"))
			dispatchLoad();
		ImGui::SameLine();
		if (ImGui::Button("Add continuation..."))
		{
			const auto picked = Platform::PickFolder(m_Directory);
			if (!picked)
				m_Error = picked.Error().technicalDetails;
			else if (*picked)
			{
				Run run;
				run.directory = **picked;
				if (!std::ranges::any_of(m_Chain, [&](const Run &existing)
					{ return existing.directory == run.directory && existing.outcarName == run.outcarName; }))
				{
					m_Chain.push_back(std::move(run));
					m_SortPending = true;
					dispatchRun(m_Chain.back(), m_AutoBands ? 0 : m_BandStart, m_AutoBands ? 0 : m_BandEnd);
				}
			}
		}
		if (ImGui::Button("Add saved OUTCAR..."))
		{
			const auto picked = Platform::PickOpenFile(m_Directory, "OUTCAR (including saved copies)", "");
			if (!picked)
				m_Error = picked.Error().technicalDetails;
			else if (*picked)
			{
				Run run;
				run.directory = (**picked).parent_path();
				run.outcarName = (**picked).filename().Utf8();
				if (!std::ranges::any_of(m_Chain, [&](const Run &existing)
					{ return existing.directory == run.directory && existing.outcarName == run.outcarName; }))
				{
					m_Chain.push_back(std::move(run));
					m_SortPending = true;
					dispatchRun(m_Chain.back(), m_AutoBands ? 0 : m_BandStart, m_AutoBands ? 0 : m_BandEnd);
				}
			}
		}
		ImGui::EndDisabled();
		if (loading)
		{
			ImGui::SameLine();
			ImGui::TextDisabled("Loading...");
		}
		for (std::size_t i = 0; i < m_Chain.size(); ++i)
		{
			ImGui::PushID(static_cast<int>(i));
			const auto &run = m_Chain[i];
			const std::string label = std::to_string(i + 1) + ". " + run.directory.filename().Utf8() + " / " + run.outcarName;
			if (ImGui::Selectable(label.c_str(), m_DetailRun == static_cast<int>(i)))
				m_DetailRun = static_cast<int>(i);
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("%s", run.directory.Utf8().c_str());
			ImGui::BeginDisabled(loading);
			ImGui::BeginDisabled(i == 0);
			if (ImGui::SmallButton("Up"))
			{
				std::swap(m_Chain[i], m_Chain[i - 1]);
				m_DetailRun = static_cast<int>(i - 1);
			}
			ImGui::EndDisabled();
			ImGui::SameLine();
			ImGui::BeginDisabled(i + 1 == m_Chain.size());
			if (ImGui::SmallButton("Down"))
			{
				std::swap(m_Chain[i], m_Chain[i + 1]);
				m_DetailRun = static_cast<int>(i + 1);
			}
			ImGui::EndDisabled();
			ImGui::SameLine();
			const bool remove = ImGui::SmallButton("Remove");
			ImGui::EndDisabled();
			if (!run.error.empty())
				ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", run.error.c_str());
			ImGui::PopID();
			if (remove)
			{
				m_Chain.erase(m_Chain.begin() + static_cast<std::ptrdiff_t>(i));
				m_DetailRun = std::max(0, static_cast<int>(m_Chain.size()) - 1);
				break;
			}
		}
	}

	void CalculationSummaryPanel::Render()
	{
		if (!IsVisible())
			return;
		ImGui::SetNextWindowSize(ImVec2(640.0f, 800.0f), ImGuiCond_FirstUseEver);
		bool stillOpen = true;
		if (!ImGui::Begin(GetTitle().c_str(), &stillOpen))
		{
			ImGui::End();
			return;
		}
		if (!stillOpen)
			SetVisible(false);
		pollJob();
		if (m_Directory.Empty())
		{
			ImGui::TextDisabled("Right-click a folder in the Project Tree and choose \"Show Calculation Summary\".");
			ImGui::End();
			return;
		}
		ImGui::SetWindowFontScale(1.2f);
		ImGui::TextDisabled("%s", m_Directory.Utf8().c_str());
		renderChain();
		if (!m_Error.empty())
			ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", m_Error.c_str());
		if (m_DetailRun >= 0 && m_DetailRun < static_cast<int>(m_Chain.size()) && m_Chain[m_DetailRun].data)
			renderSummary(*m_Chain[m_DetailRun].data);
		renderPlots();
		ImGui::SetWindowFontScale(1.0f);
		ImGui::End();
	}
} // namespace DefectStudio
