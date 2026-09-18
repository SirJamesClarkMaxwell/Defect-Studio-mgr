#pragma once

namespace DefectStudio
{
	// The Scene Outliner's two per-row columns, Blender's split: the eye (drawn in the viewport,
	// what H toggles) and the camera (drawn in an exported render). The state logic lives in
	// Renderer/Scene/SceneVisibility.hpp; this is only the pair of widgets overlaid at the right of
	// the row item drawn immediately before it.

	struct SceneVisibilityColumnState
	{
		bool visible = true;
		bool renderable = true;
		// Parent rows whose children disagree draw ImGui's mixed state instead of on or off.
		bool visibleMixed = false;
		bool renderableMixed = false;
	};

	struct SceneVisibilityColumnEdit
	{
		bool visibleChanged = false;
		bool renderableChanged = false;
		bool visible = true;
		bool renderable = true;
	};

	// Accumulates the two flags for parent rows. Empty groups read as enabled; disagreement between
	// children becomes the mixed marker without adding a second visibility model beside the objects.
	struct SceneVisibilityColumnAccumulator
	{
		bool anyVisible = false;
		bool anyHidden = false;
		bool anyRenderable = false;
		bool anyNotRenderable = false;

		void Add(bool visible, bool renderable)
		{
			anyVisible = anyVisible || visible;
			anyHidden = anyHidden || !visible;
			anyRenderable = anyRenderable || renderable;
			anyNotRenderable = anyNotRenderable || !renderable;
		}
	};

	[[nodiscard]] inline SceneVisibilityColumnState SceneVisibilityStateFrom(
		const SceneVisibilityColumnAccumulator &flags)
	{
		return {
			!flags.anyHidden, !flags.anyNotRenderable,
			flags.anyVisible && flags.anyHidden, flags.anyRenderable && flags.anyNotRenderable};
	}

	template <typename Objects>
	void AddSceneVisibilityColumnFlags(const Objects &objects, SceneVisibilityColumnAccumulator &flags)
	{
		for (const auto &object : objects)
			flags.Add(object.visible, object.renderable);
	}

	template <typename Objects>
	[[nodiscard]] SceneVisibilityColumnState SceneVisibilityStateFor(const Objects &objects)
	{
		SceneVisibilityColumnAccumulator flags;
		AddSceneVisibilityColumnFlags(objects, flags);
		return SceneVisibilityStateFrom(flags);
	}

	template <typename Objects>
	void ApplySceneVisibilityColumnEdit(Objects &objects, const SceneVisibilityColumnEdit &edit)
	{
		for (auto &object : objects)
		{
			if (edit.visibleChanged)
				object.visible = edit.visible;
			if (edit.renderableChanged)
				object.renderable = edit.renderable;
		}
	}

	// Draws both toggles over the right edge of the preceding row and restores the cursor below that
	// row. A mixed column reports the value it was clicked to, so clicking a mixed parent applies one
	// value to every child.
	[[nodiscard]] SceneVisibilityColumnEdit DrawSceneVisibilityColumns(const SceneVisibilityColumnState &state);

	// Leaf-row convenience: writes through and reports whether anything changed.
	bool DrawSceneVisibilityColumns(bool &visible, bool &renderable);
} // namespace DefectStudio
