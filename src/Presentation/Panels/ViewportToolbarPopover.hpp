#pragma once

#include <cstdint>

namespace DefectStudio
{
	struct ViewportToolbarPopoverSpec
	{
		const char *buttonId = nullptr;
		const char *popupId = nullptr;
		std::uint32_t textureId = 0;
		const char *fallbackIcon = nullptr;
		const char *text = nullptr;
		const char *tooltip = nullptr;
		float uiScale = 1.0f;
		float iconExtent = 18.0f;
		float minimumPopupWidth = 160.0f;
	};

	// Draws one header button and begins its popup. Call ImGui::EndPopup() when true.
	[[nodiscard]] bool BeginViewportToolbarPopover(const ViewportToolbarPopoverSpec &spec);
} // namespace DefectStudio
