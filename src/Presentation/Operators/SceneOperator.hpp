#pragma once

#include <functional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

#include <glm/glm.hpp>

#include "Core/Diagnostics/StructuredError.hpp"
#include "Renderer/Scene/SceneObject.hpp"

namespace DefectStudio
{
	struct RendererWindowState;

	// Who owns the undo entry. An operator re-runs many times behind one entry, so the runner pushes
	// the snapshot once and every re-run suppresses its own; a plain menu item still pushes its own.
	enum class SceneOperationUndo
	{
		Push,
		Suppress
	};

	// One tunable value. The redo panel renders a widget from this and knows nothing about what the
	// operator builds - that is the whole point of the schema being data rather than code.
	struct SceneOperatorParameter
	{
		enum class Kind
		{
			Float,
			Int,
			Bool,
			Enum,
			Color
		};

		std::string key;
		std::string label;
		Kind kind = Kind::Float;
		float minimum = 0.0f;
		float maximum = 1.0f;
		// Kind::Enum only. The stored value is the index into this list.
		std::vector<std::string> enumLabels;
	};

	using SceneOperatorValue = std::variant<float, int, bool, glm::vec3>;
	using SceneOperatorValues = std::unordered_map<std::string, SceneOperatorValue>;

	// A named, re-runnable scene operation. `execute` must be callable any number of times on the
	// same window: the panel restores the pre-operation snapshot before each re-run. It must NOT
	// push an undo entry of its own - pass SceneOperationUndo::Suppress to whatever it calls. The
	// runner pushes exactly one entry around the whole run-and-adjust session.
	struct SceneOperator
	{
		std::string id;
		// Shown in the panel header and used as the undo entry's description.
		std::string label;
		std::vector<SceneOperatorParameter> schema;
		SceneOperatorValues defaults;
		std::function<Result<std::vector<SceneObjectId>>(RendererWindowState &, const SceneOperatorValues &)>
			execute;
		// Empty means all parameters are shown. Evaluated with the operator's input selection.
		std::function<bool(const std::string &key, const SceneOperatorValues &, const RendererWindowState &)>
			isParameterRelevant;
	};
}
