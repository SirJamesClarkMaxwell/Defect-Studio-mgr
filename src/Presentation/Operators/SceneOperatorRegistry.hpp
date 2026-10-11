#pragma once

#include <string>
#include <optional>
#include <unordered_map>
#include <vector>

#include "Core/Diagnostics/StructuredError.hpp"
#include "Presentation/Operators/SceneOperator.hpp"

namespace DefectStudio
{
	// Flat id -> operator map. Registration is a startup-time act; lookup happens per frame, so Find
	// returns a borrowed pointer rather than a copy.
	class SceneOperatorRegistry
	{
	public:
		// Rejects a duplicate id with a StructuredError and keeps the first registration.
		[[nodiscard]] Result<void> Register(SceneOperator op);

		[[nodiscard]] const SceneOperator *Find(const std::string &id) const;

		// Sorted, so a caller can present the list without re-sorting an unordered_map.
		[[nodiscard]] std::vector<std::string> ListIds() const;

	private:
		std::unordered_map<std::string, SceneOperator> m_Operators;
	};

	// Registers the C_n arrow operator under the id "scene.curved_arrow". Its schema and defaults
	// mirror CurvedArrowParameters field by field.
	[[nodiscard]] Result<void> RegisterCurvedArrowOperator(SceneOperatorRegistry &registry);
	[[nodiscard]] Result<void> RegisterHideVolumeOperator(SceneOperatorRegistry &registry);
	enum class HideVolumeKind;
	// Contextual defaults and insertion target stay in the panel's values across every re-run.
	[[nodiscard]] SceneOperatorValues MakeHideVolumeOperatorValues(const RendererWindowState &window,
		HideVolumeKind kind, std::optional<std::size_t> targetAtom = std::nullopt,
		std::optional<glm::vec3> targetPosition = std::nullopt);
}
