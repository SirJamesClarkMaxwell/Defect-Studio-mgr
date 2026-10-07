#pragma once

#include <string>
#include <vector>

#include "Core/JobSystem/JobSystem.hpp"
#include "Core/Utils/Memory.hpp"
#include "Renderer/RendererWindowState.hpp"

namespace DefectStudio
{
	class RendererLayer;
	class VaspDensityGridJob;

	// task/83: turns RendererWindowState::SceneDensity requests into loaded grids. Once per frame it
	// starts a VaspDensityGridJob for every Pending density and hands finished grids back to their
	// object - so a new density, one whose component changed and one restored from a project all
	// load through the same path, and no UI code ever waits on Python.
	//
	// A finished job is applied only if the object still asks for exactly what the job read; an
	// object edited mid-load is Pending again by then and simply gets a fresh job.
	class SceneDensityLoader
	{
	public:
		SceneDensityLoader(RendererLayer &layer, WeakRef<JobSystem> jobSystem);

		void Update();

	private:
		struct InFlight
		{
			std::string windowId;
			SceneObjectId id;
			Path chgcarPath;
			Path referencePath;
			DensityComponent component = DensityComponent::Total;
			Ref<VaspDensityGridJob> job;
			JobId jobId = 0;
		};

		void dispatchPending(RendererWindowState &window, JobSystem &jobSystem);
		void collectFinished(JobSystem &jobSystem);
		[[nodiscard]] bool isInFlight(const std::string &windowId, SceneObjectId id) const;

		RendererLayer &m_Layer;
		WeakRef<JobSystem> m_JobSystem;
		std::vector<InFlight> m_InFlight;
	};
} // namespace DefectStudio
