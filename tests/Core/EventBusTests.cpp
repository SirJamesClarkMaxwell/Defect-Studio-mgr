#include <gtest/gtest.h>

#include <vector>

#include "Core/EventBus.hpp"

namespace
{
	struct TestMessage
	{
		int value = 0;
	};
} // namespace

TEST(EventBusTests, PublishInvokesSubscriber)
{
	DefectStudio::EventBus bus;
	int observedValue = 0;

	bus.Subscribe<TestMessage>([&observedValue](const TestMessage &message) { observedValue = message.value; });

	bus.Publish(TestMessage{42});
	EXPECT_EQ(observedValue, 42);
}

TEST(EventBusTests, UnsubscribeStopsFurtherDelivery)
{
	DefectStudio::EventBus bus;
	int callCount = 0;

	const DefectStudio::EventBus::SubscriptionId id =
	    bus.Subscribe<TestMessage>([&callCount](const TestMessage &) { ++callCount; });

	EXPECT_TRUE(bus.Unsubscribe<TestMessage>(id));
	bus.Publish(TestMessage{7});
	EXPECT_EQ(callCount, 0);
}

TEST(EventBusTests, SubscribersAreCalledInSubscriptionOrder)
{
	DefectStudio::EventBus bus;
	std::vector<int> order;

	bus.Subscribe<TestMessage>([&order](const TestMessage &) { order.push_back(1); });
	bus.Subscribe<TestMessage>([&order](const TestMessage &) { order.push_back(2); });

	bus.Publish(TestMessage{5});

	const std::vector<int> expected = {1, 2};
	EXPECT_EQ(order, expected);
}
