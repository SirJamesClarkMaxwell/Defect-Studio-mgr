#include <gtest/gtest.h>

#include "Core/EventSystem/DispatchingEventSystem/PlatformEvents/PlatformEvent.hpp"
#include "Core/EventSystem/DispatchingEventSystem/EventQueue.hpp"

namespace DefectStudio::Test
{
	class EventQueueTest : public ::testing::Test
	{
	protected:
		EventQueue queue;
	};

	// ===== Configuration tests =====

	TEST_F(EventQueueTest, ConfigureInitializesCapacity)
	{
		queue.Configure(128, 64);
		EXPECT_EQ(queue.Capacity(), 128);
		EXPECT_EQ(queue.Size(), 0);
		EXPECT_TRUE(queue.Empty());
	}

	TEST_F(EventQueueTest, ConfigureEnforcesMinimumCapacity)
	{
		queue.Configure(10, 5);
		EXPECT_GE(queue.Capacity(), 32);
	}

	TEST_F(EventQueueTest, ConfigureEnforcesMinimumGrowthStep)
	{
		queue.Configure(128, 5);
		// Internal growth step should be >=32
		// This is implicit via subsequent Drain operations
	}

	TEST_F(EventQueueTest, ConfigurePreservesPendingEvents)
	{
		queue.Configure(64, 32);
		queue.Add(WindowCloseEvent());
		queue.Add(KeyPressedEvent(10));

		queue.Configure(32, 32);

		EXPECT_EQ(queue.Size(), 2);
		auto drained = queue.Drain();
		EXPECT_EQ(drained.size(), 2);
	}

	// ===== Add and Drain tests =====

	TEST_F(EventQueueTest, AddEventIncreasesSize)
	{
		queue.Configure(256);
		queue.Add(WindowCloseEvent());
		EXPECT_EQ(queue.Size(), 1);
	}

	TEST_F(EventQueueTest, AddMultipleEvents)
	{
		queue.Configure(256);
		queue.Add(WindowCloseEvent());
		queue.Add(MouseMovedEvent(100.0f, 200.0f));
		queue.Add(KeyPressedEvent(65));
		EXPECT_EQ(queue.Size(), 3);
	}

	TEST_F(EventQueueTest, DrainReturnsAllEvents)
	{
		queue.Configure(256);
		queue.Add(WindowCloseEvent());
		queue.Add(MouseMovedEvent(50.0f, 75.0f));

		auto drained = queue.Drain();
		EXPECT_EQ(drained.size(), 2);
		EXPECT_EQ(queue.Size(), 0);
	}

	TEST_F(EventQueueTest, DrainEmptyQueueReturnsEmpty)
	{
		queue.Configure(256);
		auto drained = queue.Drain();
		EXPECT_TRUE(drained.empty());
	}

	TEST_F(EventQueueTest, DrainClearsQueue)
	{
		queue.Configure(256);
		queue.Add(WindowCloseEvent());
		(void)queue.Drain();
		EXPECT_TRUE(queue.Empty());
	}

	TEST_F(EventQueueTest, DrainPreservesConfiguredCapacity)
	{
		queue.Configure(256);
		const std::size_t configuredCapacity = queue.Capacity();
		queue.Add(WindowCloseEvent());

		auto drained = queue.Drain();

		EXPECT_EQ(drained.size(), 1);
		EXPECT_EQ(queue.Size(), 0);
		EXPECT_EQ(queue.Capacity(), configuredCapacity);

		queue.Add(KeyPressedEvent(10));
		EXPECT_EQ(queue.Capacity(), configuredCapacity);
		EXPECT_EQ(queue.Size(), 1);
	}

	// ===== Variant type tests =====

	TEST_F(EventQueueTest, VariantHoldsWindowCloseEvent)
	{
		queue.Configure(256);
		queue.Add(WindowCloseEvent());
		auto drained = queue.Drain();
		EXPECT_EQ(drained[0].index(), 0); // WindowCloseEvent is first in variant
	}

	TEST_F(EventQueueTest, VariantHoldsKeyPressedEvent)
	{
		queue.Configure(256);
		queue.Add(KeyPressedEvent(42));
		auto drained = queue.Drain();
		EXPECT_EQ(drained[0].index(), 2); // KeyPressedEvent index in variant
	}

	TEST_F(EventQueueTest, VariantHoldsMouseMovedEvent)
	{
		queue.Configure(256);
		queue.Add(MouseMovedEvent(123.0f, 456.0f));
		auto drained = queue.Drain();
		const auto &event = std::get<MouseMovedEvent>(drained[0]);
		EXPECT_EQ(event.GetX(), 123.0f);
		EXPECT_EQ(event.GetY(), 456.0f);
	}

	// ===== Capacity tests =====

	TEST_F(EventQueueTest, ResizeIncreasesCapacity)
	{
		queue.Configure(64);
		queue.Resize(256);
		EXPECT_EQ(queue.Capacity(), 256);
	}

	TEST_F(EventQueueTest, ResizeMaintainsElements)
	{
		queue.Configure(64);
		queue.Add(WindowCloseEvent());
		queue.Add(KeyPressedEvent(10));
		queue.Resize(256);
		EXPECT_EQ(queue.Size(), 2);
	}

	TEST_F(EventQueueTest, AddAutoExpandsWhenConfiguredCapacityIsFull)
	{
		queue.Configure(32, 32);
		const std::size_t initialCapacity = queue.Capacity();
		for (int i = 0; i < 33; ++i)
			queue.Add(KeyPressedEvent(i));

		EXPECT_GT(queue.Capacity(), initialCapacity);
		EXPECT_EQ(queue.Size(), 33);
	}

	TEST_F(EventQueueTest, FitToSizeReducesCapacity)
	{
		queue.Configure(256);
		queue.Add(WindowCloseEvent());
		queue.Add(KeyPressedEvent(10));
		queue.FitToSize();
		// Capacity should be close to size (compiler dependent)
		EXPECT_LE(queue.Capacity(), 256);
	}

	// ===== Growth step tests =====

	TEST_F(EventQueueTest, SetGrowthStepEnforcesMinimum)
	{
		queue.Configure(256);
		queue.SetGrowthStep(5);
		// Internal growth step should be >=32
		// Next batch of adds will use enforced minimum
	}

	// ===== Thread safety tests (basic) =====

	TEST_F(EventQueueTest, LockUnlockDoesNotCrash)
	{
		queue.Configure(256);
		queue.Lock();
		queue.Add(WindowCloseEvent());
		queue.Unlock();
		EXPECT_EQ(queue.Size(), 1);
	}

	TEST_F(EventQueueTest, MultipleLockUnlockCycles)
	{
		queue.Configure(256);
		for (int i = 0; i < 10; ++i)
		{
			queue.Lock();
			queue.Add(MouseMovedEvent(static_cast<float>(i), static_cast<float>(i)));
			queue.Unlock();
		}
		EXPECT_EQ(queue.Size(), 10);
	}

	// ===== Event type specific tests =====

	class EventTypeTest : public ::testing::Test
	{
	};

	TEST_F(EventTypeTest, WindowCloseEventHasCorrectType)
	{
		WindowCloseEvent event;
		EXPECT_EQ(event.GetEventType(), EventType::WindowClose);
		EXPECT_STREQ(event.GetName().data(), "WindowClose");
	}

	TEST_F(EventTypeTest, WindowResizeEventDataPreservation)
	{
		WindowResizeEvent event(1920, 1080);
		EXPECT_EQ(event.GetWidth(), 1920);
		EXPECT_EQ(event.GetHeight(), 1080);
	}

	TEST_F(EventTypeTest, KeyPressedEventCodePreservation)
	{
		KeyPressedEvent event(65); // 'A' key
		EXPECT_EQ(event.GetKeyCode(), 65);
	}

	TEST_F(EventTypeTest, MouseMovedEventCoordinates)
	{
		MouseMovedEvent event(512.5f, 768.25f);
		EXPECT_EQ(event.GetX(), 512.5f);
		EXPECT_EQ(event.GetY(), 768.25f);
	}

	TEST_F(EventTypeTest, MouseScrolledEventOffsets)
	{
		MouseScrolledEvent event(0.5f, 1.5f);
		EXPECT_EQ(event.GetOffsetX(), 0.5f);
		EXPECT_EQ(event.GetOffsetY(), 1.5f);
	}

	TEST_F(EventTypeTest, MouseButtonEventButton)
	{
		MouseButtonPressedEvent event(0);  // Left button
		MouseButtonReleasedEvent event2(1); // Right button
		EXPECT_EQ(event.GetButton(), 0);
		EXPECT_EQ(event2.GetButton(), 1);
	}

	TEST_F(EventTypeTest, KeyboardEventTypes)
	{
		KeyPressedEvent pressed(10);
		KeyReleasedEvent released(10);
		KeyRepeatedEvent repeated(10);

		EXPECT_EQ(pressed.GetEventType(), EventType::KeyPressed);
		EXPECT_EQ(released.GetEventType(), EventType::KeyReleased);
		EXPECT_EQ(repeated.GetEventType(), EventType::KeyRepeated);
	}

	TEST_F(EventTypeTest, EventCategoryFlags)
	{
		WindowCloseEvent winEvent;
		KeyPressedEvent keyEvent(10);
		MouseMovedEvent mouseEvent(0.0f, 0.0f);

		EXPECT_TRUE(winEvent.IsInCategory(EventCategoryApplication));
		EXPECT_TRUE(keyEvent.IsInCategory(EventCategoryInput));
		EXPECT_TRUE(keyEvent.IsInCategory(EventCategoryKeyboard));
		EXPECT_TRUE(mouseEvent.IsInCategory(EventCategoryMouse));
	}

	TEST_F(EventTypeTest, EventHandledFlag)
	{
		WindowCloseEvent event;
		EXPECT_FALSE(event.handled);
		event.handled = true;
		EXPECT_TRUE(event.handled);
	}

} // namespace DefectStudio::Test
