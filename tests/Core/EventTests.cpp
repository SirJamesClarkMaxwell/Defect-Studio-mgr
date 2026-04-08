#include <gtest/gtest.h>

#include "Core/Event.hpp"

TEST(EventTests, DispatchesOnlyMatchingType)
{
	DefectStudio::WindowResizeEvent resizeEvent(1280, 720);
	DefectStudio::Event &event = resizeEvent;
	DefectStudio::EventDispatcher dispatcher(event);

	bool called = false;
	const bool dispatched = dispatcher.Dispatch<DefectStudio::WindowResizeEvent>(
	    [&called](DefectStudio::WindowResizeEvent &typedEvent)
	    {
		    called = true;
		    EXPECT_EQ(typedEvent.GetWidth(), 1280);
		    EXPECT_EQ(typedEvent.GetHeight(), 720);
		    return true;
	    });

	EXPECT_TRUE(dispatched);
	EXPECT_TRUE(called);
	EXPECT_TRUE(event.handled);
}

TEST(EventTests, NonMatchingDispatchDoesNothing)
{
	DefectStudio::WindowCloseEvent closeEvent;
	DefectStudio::Event &event = closeEvent;
	DefectStudio::EventDispatcher dispatcher(event);

	bool called = false;
	const bool dispatched = dispatcher.Dispatch<DefectStudio::KeyPressedEvent>(
	    [&called](DefectStudio::KeyPressedEvent &)
	    {
		    called = true;
		    return true;
	    });

	EXPECT_FALSE(dispatched);
	EXPECT_FALSE(called);
	EXPECT_FALSE(event.handled);
}

TEST(EventTests, HandledFlagCanStopFurtherDispatch)
{
	DefectStudio::WindowCloseEvent closeEvent;
	DefectStudio::Event &event = closeEvent;
	DefectStudio::EventDispatcher dispatcher(event);

	int callCount = 0;
	dispatcher.Dispatch<DefectStudio::WindowCloseEvent>(
	    [&callCount](DefectStudio::WindowCloseEvent &)
	    {
		    ++callCount;
		    return true;
	    });

	if (!event.handled)
	{
		dispatcher.Dispatch<DefectStudio::WindowCloseEvent>(
		    [&callCount](DefectStudio::WindowCloseEvent &)
		    {
			    ++callCount;
			    return true;
		    });
	}

	EXPECT_EQ(callCount, 1);
}

TEST(EventTests, CategoriesAreExposedCorrectly)
{
	DefectStudio::KeyPressedEvent keyEvent(65);
	EXPECT_TRUE(keyEvent.IsInCategory(DefectStudio::EventCategoryInput));
	EXPECT_TRUE(keyEvent.IsInCategory(DefectStudio::EventCategoryKeyboard));
	EXPECT_FALSE(keyEvent.IsInCategory(DefectStudio::EventCategoryMouse));

	DefectStudio::TouchpadGestureEvent touchpadEvent(0.2f, -0.3f);
	EXPECT_TRUE(touchpadEvent.IsInCategory(DefectStudio::EventCategoryTouchpad));
	EXPECT_TRUE(touchpadEvent.IsInCategory(DefectStudio::EventCategoryInput));
	EXPECT_FALSE(touchpadEvent.IsInCategory(DefectStudio::EventCategoryApplication));

	DefectStudio::KeyReleasedEvent keyReleasedEvent(65);
	EXPECT_TRUE(keyReleasedEvent.IsInCategory(DefectStudio::EventCategoryKeyboard));

	DefectStudio::KeyRepeatedEvent keyRepeatedEvent(65);
	EXPECT_TRUE(keyRepeatedEvent.IsInCategory(DefectStudio::EventCategoryKeyboard));
	EXPECT_TRUE(keyRepeatedEvent.IsInCategory(DefectStudio::EventCategoryInput));

	DefectStudio::MouseButtonPressedEvent mousePressedEvent(0);
	EXPECT_TRUE(mousePressedEvent.IsInCategory(DefectStudio::EventCategoryMouse));
	EXPECT_TRUE(mousePressedEvent.IsInCategory(DefectStudio::EventCategoryMouseButton));

	DefectStudio::MouseButtonReleasedEvent mouseReleasedEvent(0);
	EXPECT_TRUE(mouseReleasedEvent.IsInCategory(DefectStudio::EventCategoryMouse));
	EXPECT_TRUE(mouseReleasedEvent.IsInCategory(DefectStudio::EventCategoryMouseButton));

	DefectStudio::MouseScrolledEvent mouseScrolledEvent(0.0f, 1.0f);
	EXPECT_TRUE(mouseScrolledEvent.IsInCategory(DefectStudio::EventCategoryMouse));
	EXPECT_FALSE(mouseScrolledEvent.IsInCategory(DefectStudio::EventCategoryMouseButton));
}
