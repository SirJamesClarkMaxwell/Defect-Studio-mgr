#pragma once

#include <array>
#include <string>

#include "Core/Utils/Memory.hpp"
#include "Presentation/EditorUiState.hpp"
#include "Presentation/Panels/IPanel.hpp"

namespace DefectStudio
{
	class EventBus;

	class AppearanceEditor final : public IPanel
	{
	public:
		explicit AppearanceEditor(Ref<EventBus> eventBus = {},
		                          WeakRef<EditorUiState> uiState = {},
		                          std::string title = "Appearance Editor",
		                          bool visibleByDefault = false);

		void Render() override;
		[[nodiscard]] Ref<IPanel> Clone() const override;

	private:
		void initializeBuffers(const EditorUiState &uiState);
		void syncBuffersToState(EditorUiState &uiState);
		void renderColorSection(AppearanceConfig &appearance);
		void renderMetricsSection(AppearanceConfig &appearance);
		void renderStateRulesSection(AppearanceConfig &appearance);
		void renderFileSection(EditorUiState &uiState);

	private:
		Ref<EventBus> m_EventBus;
		WeakRef<EditorUiState> m_UiState;
		bool m_BuffersInitialized = false;
		std::array<char, 320> m_ThemeSavePath = {};
		std::array<char, 320> m_ThemeLoadPath = {};
		std::array<char, 320> m_LayoutPath = {};
	};
} // namespace DefectStudio
