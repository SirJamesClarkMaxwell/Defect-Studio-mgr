#pragma once

#include <glm/glm.hpp>
#include "Core/Utils/Memory.hpp"

namespace DefectStudio
{
	class CommandRegistry;
	class EventBus;
	class OperatorRedoPanel;
	class SceneOperatorRegistry;
	struct RendererWindowState;
	// One catalogue for RMB, Shift+A, and every toolbar Add popup. Atom uses the existing
	// coordinate editor; only the insertion position differs between entry points.
	void DrawSceneAddMenu(RendererWindowState &windowState, const WeakRef<CommandRegistry> &commands,
		const glm::vec3 &position, const Ref<EventBus> &eventBus, bool fractionalAtomPosition = false,
		OperatorRedoPanel *redoPanel = nullptr, SceneOperatorRegistry *operatorRegistry = nullptr);
}
