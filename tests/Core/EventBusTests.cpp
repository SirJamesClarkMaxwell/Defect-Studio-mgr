#include <gtest/gtest.h>

#include <atomic>
#include <exception>
#include <thread>
#include <vector>

#include "Core/Utils/Memory.hpp"
#include "Core/EventSystem/BusEventSystem/Event.hpp"
#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Core/EventSystem/BusEventSystem/EventPriority.hpp"

namespace
{
	struct TestEvent : public DefectStudio::BusEvent
	{
		int value = 0;

		explicit TestEvent(int newValue = 0) : value(newValue)
		{
		}
	};
} // namespace

TEST(EventBusTests, SubscribeReturnsValidHandle)
{
	auto bus = DefectStudio::CreateRef<DefectStudio::EventBus>();
	auto handle = bus->Subscribe<TestEvent>([](const TestEvent &) {});
	EXPECT_TRUE(handle.IsValid());
}

TEST(EventBusTests, SubscriptionHandleDestructorUnsubscribes)
{
	auto bus = DefectStudio::CreateRef<DefectStudio::EventBus>();
	int callCount = 0;
	{
		auto handle = bus->Subscribe<TestEvent>([&callCount](const TestEvent &) { ++callCount; });
		EXPECT_TRUE(handle.IsValid());
	}

	bus->Publish(TestEvent{1});
	EXPECT_EQ(callCount, 0);
}

TEST(EventBusTests, MoveAssignTransfersSubscription)
{
	auto bus = DefectStudio::CreateRef<DefectStudio::EventBus>();
	int callCount = 0;

	auto handleA = bus->Subscribe<TestEvent>([&callCount](const TestEvent &) { ++callCount; });
	DefectStudio::SubscriptionHandle handleB;
	handleB = std::move(handleA);

	EXPECT_FALSE(handleA.IsValid());
	EXPECT_TRUE(handleB.IsValid());

	bus->Publish(TestEvent{1});
	EXPECT_EQ(callCount, 1);
}

TEST(EventBusTests, PublishDeliversToAllSubscribers)
{
	auto bus = DefectStudio::CreateRef<DefectStudio::EventBus>();
	int callCount = 0;

	std::vector<DefectStudio::SubscriptionHandle> handles;
	handles.push_back(bus->Subscribe<TestEvent>([&callCount](const TestEvent &) { ++callCount; }));
	handles.push_back(bus->Subscribe<TestEvent>([&callCount](const TestEvent &) { ++callCount; }));

	bus->Publish(TestEvent{1});
	EXPECT_EQ(callCount, 2);
}

TEST(EventBusTests, PriorityOrderIsHighestFirst)
{
	auto bus = DefectStudio::CreateRef<DefectStudio::EventBus>();
	std::vector<int> order;
	std::vector<DefectStudio::SubscriptionHandle> handles;

	handles.push_back(bus->Subscribe<TestEvent>([&order](const TestEvent &) { order.push_back(2); },
	                                          DefectStudio::EventPriority::Normal));
	handles.push_back(bus->Subscribe<TestEvent>([&order](const TestEvent &) { order.push_back(1); },
	                                          DefectStudio::EventPriority::Highest));
	handles.push_back(bus->Subscribe<TestEvent>([&order](const TestEvent &) { order.push_back(3); },
	                                          DefectStudio::EventPriority::Low));

	bus->Publish(TestEvent{1});

	const std::vector<int> expected = {1, 2, 3};
	EXPECT_EQ(order, expected);
}

TEST(EventBusTests, EqualPriorityPreservesSubscriptionOrder)
{
	auto bus = DefectStudio::CreateRef<DefectStudio::EventBus>();
	std::vector<int> order;
	std::vector<DefectStudio::SubscriptionHandle> handles;

	handles.push_back(bus->Subscribe<TestEvent>([&order](const TestEvent &) { order.push_back(1); },
	                                          DefectStudio::EventPriority::Normal));
	handles.push_back(bus->Subscribe<TestEvent>([&order](const TestEvent &) { order.push_back(2); },
	                                          DefectStudio::EventPriority::Normal));

	bus->Publish(TestEvent{1});

	const std::vector<int> expected = {1, 2};
	EXPECT_EQ(order, expected);
}

TEST(EventBusTests, StopPropagationPreventsRemainingHandlers)
{
	auto bus = DefectStudio::CreateRef<DefectStudio::EventBus>();
	int callCount = 0;
	std::vector<DefectStudio::SubscriptionHandle> handles;

	handles.push_back(bus->Subscribe<TestEvent>([&callCount](const TestEvent &event) {
		++callCount;
		const_cast<TestEvent &>(event).stopPropagation = true;
	}));
	handles.push_back(bus->Subscribe<TestEvent>([&callCount](const TestEvent &) { ++callCount; }));

	bus->Publish(TestEvent{1});
	EXPECT_EQ(callCount, 1);
}

TEST(EventBusTests, HandledFlagDoesNotStopPropagation)
{
	auto bus = DefectStudio::CreateRef<DefectStudio::EventBus>();
	int callCount = 0;
	std::vector<DefectStudio::SubscriptionHandle> handles;

	handles.push_back(bus->Subscribe<TestEvent>([&callCount](const TestEvent &event) {
		++callCount;
		const_cast<TestEvent &>(event).handled = true;
	}));
	handles.push_back(bus->Subscribe<TestEvent>([&callCount](const TestEvent &) { ++callCount; }));

	bus->Publish(TestEvent{1});
	EXPECT_EQ(callCount, 2);
}

TEST(EventBusTests, UnsubscribeDuringDispatchDefersRemoval)
{
	auto bus = DefectStudio::CreateRef<DefectStudio::EventBus>();
	int callCount = 0;

	DefectStudio::SubscriptionHandle handleA;
	DefectStudio::SubscriptionHandle handleB;
	handleA = bus->Subscribe<TestEvent>([&handleB, &callCount](const TestEvent &) {
		++callCount;
		handleB.Reset();
	});
	handleB = bus->Subscribe<TestEvent>([&callCount](const TestEvent &) { ++callCount; });

	bus->Publish(TestEvent{1});
	EXPECT_EQ(callCount, 1);
}

TEST(EventBusTests, SubscriberAddedDuringDispatchNotCalledSameCycle)
{
	auto bus = DefectStudio::CreateRef<DefectStudio::EventBus>();
	int firstCount = 0;
	int secondCount = 0;
	DefectStudio::SubscriptionHandle lateHandle;
	DefectStudio::SubscriptionHandle firstHandle;

	firstHandle = bus->Subscribe<TestEvent>([&](const TestEvent &) {
		++firstCount;
		if (!lateHandle.IsValid())
		{
			lateHandle = bus->Subscribe<TestEvent>([&secondCount](const TestEvent &) { ++secondCount; });
		}
	});

	bus->Publish(TestEvent{1});
	EXPECT_EQ(firstCount, 1);
	EXPECT_EQ(secondCount, 0);

	bus->Publish(TestEvent{2});
	EXPECT_EQ(secondCount, 1);
}

TEST(EventBusTests, ClearAllListenersDuringDispatchDefersRemoval)
{
	auto bus = DefectStudio::CreateRef<DefectStudio::EventBus>();
	int callCount = 0;
	std::vector<DefectStudio::SubscriptionHandle> handles;

	handles.push_back(bus->Subscribe<TestEvent>([&](const TestEvent &) {
		++callCount;
		bus->ClearAllListeners();
	}));
	handles.push_back(bus->Subscribe<TestEvent>([&](const TestEvent &) { ++callCount; }));

	bus->Publish(TestEvent{1});
	EXPECT_EQ(callCount, 2);

	bus->Publish(TestEvent{2});
	EXPECT_EQ(callCount, 2);
}

TEST(EventBusTests, QueueFromWorkerThreadIsSafe)
{
	auto bus = DefectStudio::CreateRef<DefectStudio::EventBus>();
	std::atomic<int> received{0};
	DefectStudio::SubscriptionHandle handle;
	std::exception_ptr workerError;

	handle = bus->Subscribe<TestEvent>([&received](const TestEvent &) { ++received; });

	std::thread worker([&bus, &workerError]() {
		try
		{
			bus->Queue(TestEvent{1});
		}
		catch (...)
		{
			workerError = std::current_exception();
		}
	});
	worker.join();
	if (workerError)
	{
		try
		{
			std::rethrow_exception(workerError);
		}
		catch (const std::exception &ex)
		{
			FAIL() << ex.what();
		}
		catch (...)
		{
			FAIL() << "Unknown exception";
		}
	}

	EXPECT_EQ(bus->GetQueuedEventCount(), 1u);
	bus->ProcessQueue();
	EXPECT_EQ(received.load(), 1);
}

TEST(EventBusTests, ProcessQueueDeliversInSubmissionOrder)
{
	auto bus = DefectStudio::CreateRef<DefectStudio::EventBus>();
	std::vector<int> order;
	DefectStudio::SubscriptionHandle handle;

	handle = bus->Subscribe<TestEvent>([&order](const TestEvent &event) { order.push_back(event.value); });

	bus->Queue(TestEvent{1});
	bus->Queue(TestEvent{2});
	bus->ProcessQueue();

	const std::vector<int> expected = {1, 2};
	EXPECT_EQ(order, expected);
}

TEST(EventBusTests, IsDispatchingReflectsPublishScope)
{
	auto bus = DefectStudio::CreateRef<DefectStudio::EventBus>();
	bool observedDispatching = false;
	DefectStudio::SubscriptionHandle handle;

	handle = bus->Subscribe<TestEvent>([&](const TestEvent &) { observedDispatching = bus->IsDispatching(); });

	EXPECT_FALSE(bus->IsDispatching());
	bus->Publish(TestEvent{1});
	EXPECT_TRUE(observedDispatching);
	EXPECT_FALSE(bus->IsDispatching());
}

TEST(EventBusTests, GetQueuedEventCountTracksPendingEvents)
{
	auto bus = DefectStudio::CreateRef<DefectStudio::EventBus>();

	bus->Queue(TestEvent{1});
	bus->Queue(TestEvent{2});
	EXPECT_EQ(bus->GetQueuedEventCount(), 2u);

	bus->ProcessQueue();
	EXPECT_EQ(bus->GetQueuedEventCount(), 0u);
}

TEST(EventBusTests, HasSubscribersAndCountsAreAccurate)
{
	auto bus = DefectStudio::CreateRef<DefectStudio::EventBus>();

	EXPECT_FALSE(bus->HasSubscribers<TestEvent>());
	EXPECT_EQ(bus->GetSubscriberCount<TestEvent>(), 0u);
	EXPECT_EQ(bus->GetTotalSubscriberCount(), 0u);

	auto handle = bus->Subscribe<TestEvent>([](const TestEvent &) {});
	EXPECT_TRUE(handle.IsValid());
	EXPECT_TRUE(bus->HasSubscribers<TestEvent>());
	EXPECT_EQ(bus->GetSubscriberCount<TestEvent>(), 1u);
	EXPECT_EQ(bus->GetTotalSubscriberCount(), 1u);
}
