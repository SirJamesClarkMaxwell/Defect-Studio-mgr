#pragma once

#include "Core/dspch.hpp"
#include "Core/Utils/KeyCodes.hpp"
#include "Core/Utils/MouseCodes.hpp"

namespace DefectStudio
{
	struct InputBackend
	{
		std::function<bool(KeyCode)> isKeyDown;
		std::function<bool(MouseCode)> isMouseButtonDown;
		std::function<std::pair<float, float>()> mousePosition;
	};

	class Input
	{
	public:
		static void SetBackend(InputBackend backend);
		static void ResetBackend();

		[[nodiscard]] static bool IsKeyDown(KeyCode code);
		[[nodiscard]] static bool IsMouseButtonDown(MouseCode code);
		[[nodiscard]] static std::pair<float, float> GetMousePosition();
		[[nodiscard]] static float GetMouseX();
		[[nodiscard]] static float GetMouseY();
	};
} // namespace DefectStudio
