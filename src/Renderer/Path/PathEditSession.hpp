#pragma once

#include <vector>

#include "Renderer/Path/PathTypes.hpp"
#include "Renderer/Scene/SceneObject.hpp"

namespace DefectStudio
{
	// Which kind of thing a click in Edit Mode selects.
	//
	// Bound to 1 / 2 / 3, the same way Blender binds vertex / edge / face. NodeHandle is one mode and
	// not two because a Bezier handle is not a thing you switch to - it belongs to a node, it is
	// drawn tethered to that node, and picking already arbitrates Handle above Node so the whisker
	// wins when they overlap.
	enum class PathElementMode
	{
		NodeHandle,
		Segment,
		WholePath,
	};

	// What is being edited inside one path, while Edit Mode is on.
	//
	// Object Mode selects whole paths and lives in RendererWindowState::selectedScenePaths. This is
	// the level below: one path is opened, and the selection is of elements inside it. The two are
	// separate on purpose - leaving Edit Mode must leave the object selection exactly as it was.
	//
	// This type holds data and invariants only. The Replace / Toggle / Range logic is NOT duplicated
	// here: `ApplySceneOutlinerSelection` in Presentation is already a template over the id type and
	// works on PathElementId unchanged. Presentation computes the new selection and hands it over.
	// A Renderer header must not reach into Presentation anyway.
	class PathEditSession
	{
	public:
		// Opens `path` for editing. Opening a different path replaces the session and clears the
		// selection - element ids are only meaningful inside the path that owns them, so carrying
		// them across would select whatever happened to share an id.
		void Enter(SceneObjectId path);

		// Back to Object Mode. Clears the selection: a stale element selection surviving into the
		// next session is the same defect selectedScenePaths documents - it shows the wrong section
		// in the Properties panel.
		void Leave();

		[[nodiscard]] bool IsActive() const noexcept;
		[[nodiscard]] SceneObjectId Path() const noexcept;

		void SetElementMode(PathElementMode mode) noexcept;
		[[nodiscard]] PathElementMode ElementMode() const noexcept;

		// Replaces the selection wholesale. Order is meaningful: back() is the active element, the
		// one that gets the enlarged draw and pick radius and, later, the transform pivot. Same
		// convention as selectedSceneArrows and selectedScenePaths.
		//
		// Ignored when the session is not active - there is no path for the ids to belong to.
		void SetSelection(std::vector<PathElementId> selection);

		[[nodiscard]] const std::vector<PathElementId> &Selection() const noexcept;
		[[nodiscard]] bool IsSelected(PathElementId element) const noexcept;

		// The last selected element, or an unset id when nothing is selected. This is what
		// BuildPathHandleMarkers and PathPickSettings take as `activeElement`.
		[[nodiscard]] PathElementId ActiveElement() const noexcept;

		void ClearSelection() noexcept;

		// Drops selected ids that no longer exist in `path`, preserving the order of those that do.
		//
		// Call this after ANY topology change, not only after a delete the user asked for: an undo, a
		// reload and a binding refresh can all remove an element out from under a selection that
		// nobody touched. An id that outlives its element is not merely stale, it can collide with a
		// later element that reuses the number.
		//
		// A path whose id is not this session's leaves the selection alone - pruning against the
		// wrong path would empty it.
		void PruneSelection(const ScenePath &path);

	private:
		bool m_Active = false;
		SceneObjectId m_Path;
		PathElementMode m_ElementMode = PathElementMode::NodeHandle;
		std::vector<PathElementId> m_Selection;
	};
} // namespace DefectStudio
