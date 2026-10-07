#include "Core/dspch.hpp"

#include "Presentation/Panels/SceneDensityLoader.hpp"

#include <algorithm>

#include "Core/Logging/Logger.hpp"
#include "Presentation/Panels/SceneDensityEditor.hpp"
#include "Renderer/RendererLayer.hpp"
#include "ScientificRuntime/Python/VaspDensityGridJob.hpp"

namespace DefectStudio
{
	using SceneDensity = RendererWindowState::SceneDensity;

	SceneDensityLoader::SceneDensityLoader(RendererLayer &layer, WeakRef<JobSystem> jobSystem)
		: m_Layer(layer), m_JobSystem(std::move(jobSystem)) {}

	void SceneDensityLoader::Update()
	{
		const Ref<JobSystem> jobSystem = m_JobSystem.lock();
		if (jobSystem == nullptr)
			return;
		collectFinished(*jobSystem);
		for (RendererWindowState &window : m_Layer.GetWindows())
			dispatchPending(window, *jobSystem);
	}

	bool SceneDensityLoader::isInFlight(const std::string &windowId, const SceneObjectId id) const
	{
		return std::any_of(m_InFlight.begin(), m_InFlight.end(),
			[&](const InFlight &entry) { return entry.windowId == windowId && entry.id == id; });
	}

	void SceneDensityLoader::dispatchPending(RendererWindowState &window, JobSystem &jobSystem)
	{
		for (SceneDensity &density : window.sceneDensities)
		{
			// Undo can bring back an object captured mid-load; with no job behind it, ask again.
			if (density.loadState == SceneDensity::LoadState::Loading && !isInFlight(window.windowId, density.id))
				density.loadState = SceneDensity::LoadState::Pending;
			if (density.loadState != SceneDensity::LoadState::Pending || isInFlight(window.windowId, density.id))
				continue;
			if (density.chgcarPath.Empty())
			{
				density.loadState = SceneDensity::LoadState::Failed;
				density.loadError = "Nie wybrano pliku CHGCAR.";
				continue;
			}
			InFlight entry{window.windowId, density.id, density.chgcarPath, density.referencePath, density.component};
			entry.job = CreateRef<VaspDensityGridJob>(density.chgcarPath, density.component, density.referencePath);
			entry.jobId = jobSystem.Submit(entry.job, JobPriority::Normal);
			m_InFlight.push_back(std::move(entry));
			density.loadState = SceneDensity::LoadState::Loading;
			density.loadError.clear();
		}
	}

	void SceneDensityLoader::collectFinished(JobSystem &jobSystem)
	{
		std::erase_if(m_InFlight, [&](InFlight &entry) {
			const std::optional<JobSnapshot> snapshot = jobSystem.GetJob(entry.jobId);
			if (snapshot.has_value() && (snapshot->status == JobStatus::Queued || snapshot->status == JobStatus::Running))
				return false;

			auto &windows = m_Layer.GetWindows();
			const auto window = std::find_if(windows.begin(), windows.end(),
				[&](const RendererWindowState &candidate) { return candidate.windowId == entry.windowId; });
			SceneDensity *density = window == windows.end() ? nullptr : FindAnnotation(window->sceneDensities, entry.id);
			const bool stillWanted = density != nullptr && density->loadState == SceneDensity::LoadState::Loading &&
				density->chgcarPath == entry.chgcarPath && density->referencePath == entry.referencePath &&
				density->component == entry.component;
			if (!stillWanted)
				return true;

			std::optional<DensityGrid> &result = entry.job->GetResult();
			if (snapshot.has_value() && snapshot->status == JobStatus::Completed && result.has_value())
			{
				if (!(density->isoValue > 0.0f))
					density->isoValue = DefaultDensityIsoValue(result->statistics);
				density->data = CreateRef<const DensityGrid>(std::move(*result));
				density->loadState = SceneDensity::LoadState::Ready;
				DS_LOG_INFO("SceneDensityLoader: {} ({}) loaded, integral {:.4f}", entry.chgcarPath.Utf8(),
					DensityComponentKey(entry.component), density->data->statistics.integral);
			}
			else
			{
				density->loadState = SceneDensity::LoadState::Failed;
				density->loadError = snapshot.has_value() && !snapshot->errorMessage.empty()
					? snapshot->errorMessage
					: "Wczytywanie CHGCAR nie powiodło się.";
				DS_LOG_WARN("SceneDensityLoader: {} failed: {}", entry.chgcarPath.Utf8(), density->loadError);
			}
			return true;
		});
	}
} // namespace DefectStudio
