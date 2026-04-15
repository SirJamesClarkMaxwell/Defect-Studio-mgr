#include <gtest/gtest.h>

#include "Core/Utils/Input.hpp"

TEST(InputTests, UsesConfiguredBackendForKeyState)
{
	DefectStudio::InputBackend backend;
	backend.isKeyDown = [](DefectStudio::KeyCode code) { return code == DefectStudio::KeyCode::Escape; };

	DefectStudio::Input::SetBackend(std::move(backend));

	EXPECT_TRUE(DefectStudio::Input::IsKeyDown(DefectStudio::KeyCode::Escape));
	EXPECT_FALSE(DefectStudio::Input::IsKeyDown(DefectStudio::KeyCode::Enter));

	DefectStudio::Input::ResetBackend();
}

TEST(InputTests, UsesConfiguredBackendForMouseStateAndPosition)
{
	DefectStudio::InputBackend backend;
	backend.isMouseButtonDown =
	    [](DefectStudio::MouseCode code) { return code == DefectStudio::MouseCode::Left; };
	backend.mousePosition = []() { return std::make_pair(123.0f, 456.0f); };

	DefectStudio::Input::SetBackend(std::move(backend));

	EXPECT_TRUE(DefectStudio::Input::IsMouseButtonDown(DefectStudio::MouseCode::Left));
	EXPECT_FALSE(DefectStudio::Input::IsMouseButtonDown(DefectStudio::MouseCode::Right));
	EXPECT_FLOAT_EQ(DefectStudio::Input::GetMouseX(), 123.0f);
	EXPECT_FLOAT_EQ(DefectStudio::Input::GetMouseY(), 456.0f);

	DefectStudio::Input::ResetBackend();
}

TEST(InputTests, ReturnsSafeDefaultsWhenBackendIsNotConfigured)
{
	DefectStudio::Input::ResetBackend();

	EXPECT_FALSE(DefectStudio::Input::IsKeyDown(DefectStudio::KeyCode::A));
	EXPECT_FALSE(DefectStudio::Input::IsMouseButtonDown(DefectStudio::MouseCode::Left));
	const auto [x, y] = DefectStudio::Input::GetMousePosition();
	EXPECT_FLOAT_EQ(x, 0.0f);
	EXPECT_FLOAT_EQ(y, 0.0f);
}
