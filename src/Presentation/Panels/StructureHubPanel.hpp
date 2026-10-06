#pragma once

#include <array>
#include <string>
#include <unordered_map>

#include "App/CreationSession.hpp"
#include "Core/Utils/Memory.hpp"
#include "Core/Utils/Path.hpp"
#include "Presentation/Panels/IPanel.hpp"

namespace DefectStudio
{
	class EventBus;

	// Session manager for structure creation. Lists every creation session that has been handed over
	// from New Structure, shows its state and last error, and owns the ONE "Add to Project" button in
	// the application.
	//
	// It holds no draft of its own: the draft lives in CreationSessionRegistry, which the New
	// Structure panel edits and this panel reads. That is what keeps the two panels decoupled - they
	// share a registry and events, never each other.
	class StructureHubPanel final : public IPanel
	{
	public:
		StructureHubPanel(
			Ref<CreationSessionRegistry> sessionRegistry,
			Ref<EventBus> eventBus,
			std::string title = "Structure Hub",
			bool visibleByDefault = true);
		StructureHubPanel(const StructureHubPanel &other) = default;

		void Render() override;
		[[nodiscard]] PanelCategory GetCategory() const override { return PanelCategory::Structure; }
		[[nodiscard]] Ref<IPanel> Clone() const override;

		// Current Project Tree selection, pushed in on ProjectTreeSelectionChanged. This is compared
		// against what each session captured at hand-off time, not silently substituted for it.
		void SetTargetDirectory(const Path &targetDirectory);

	private:
		void drawSession(CreationSession &session);
		void drawTargetSection(CreationSession &session);
		void dispatchAddToProject(CreationSession &session);
		[[nodiscard]] std::array<char, 128> &nameBufferFor(const CreationSession &session);

		Ref<CreationSessionRegistry> m_SessionRegistry;
		Ref<EventBus> m_EventBus;
		Path m_TargetDirectory;

		// Per-session name entry, keyed by stringified sessionId so several open sessions do not
		// fight over one buffer.
		std::unordered_map<std::string, std::array<char, 128>> m_NameBuffers;
	};
} // namespace DefectStudio
