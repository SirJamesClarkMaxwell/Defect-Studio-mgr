#include <gtest/gtest.h>

#include <vector>

#include "Core/Capabilities/CapabilityService.hpp"
#include "Core/Commands/CommandRegistry.hpp"
#include "Core/Undo/UndoStack.hpp"
#include "Core/Utils/Memory.hpp"

namespace
{
	class IncrementCommand final : public DefectStudio::ICommand
	{
	public:
		IncrementCommand(DefectStudio::Ref<int> value, int amount)
			: m_Value(std::move(value)), m_Amount(amount)
		{
		}

		DefectStudio::Result<void> Execute(DefectStudio::CommandContext &context) override
		{
			(void)context;
			*m_Value += m_Amount;
			return {};
		}

		DefectStudio::Result<void> Undo(DefectStudio::CommandContext &context) override
		{
			(void)context;
			*m_Value -= m_Amount;
			return {};
		}

		std::string Description() const override
		{
			return "Increment";
		}

		bool IsUndoable() const noexcept override
		{
			return true;
		}

	private:
		DefectStudio::Ref<int> m_Value;
		int m_Amount = 0;
	};

	class FailingCommand final : public DefectStudio::ICommand
	{
	public:
		DefectStudio::Result<void> Execute(DefectStudio::CommandContext &context) override
		{
			(void)context;
			return DefectStudio::StructuredError{
				DefectStudio::ErrorCategory::Validation,
				DefectStudio::Severity::Error,
				"fail",
				"intentional test failure",
				"fix the test command",
				"FailingCommand",
				"test.command.failure"};
		}

		std::string Description() const override
		{
			return "Fail";
		}
	};
} // namespace

TEST(CommandContextTests, StoresAndRetrievesTypedValues)
{
	DefectStudio::CommandContext context;
	context.Set("amount", 7);

	auto amount = context.GetCopy<int>("amount");

	ASSERT_TRUE(amount);
	EXPECT_EQ(amount.Value(), 7);
	EXPECT_EQ(context.GetCopy<std::string>("amount").Error().code, "command.context.missing_value");
}

TEST(CommandRegistryTests, ExecutesFactoryCommandAndPushesUndo)
{
	auto value = DefectStudio::CreateRef<int>(0);
	DefectStudio::CommandRegistry registry;
	auto undoStack = DefectStudio::CreateRef<DefectStudio::UndoStack>();
	registry.SetUndoStack(DefectStudio::CreateWeakRef(undoStack));

	auto registered = registry.Register(
		DefectStudio::CommandMeta{
			DefectStudio::CommandID("test.increment"),
			"Increment",
			"Test",
			"Increment shared test value",
			{},
			DefectStudio::CommandFlags::Undoable},
		[value](DefectStudio::CommandContext &) {
			return std::make_unique<IncrementCommand>(value, 3);
		});
	ASSERT_TRUE(registered);

	auto result = registry.Execute(DefectStudio::CommandID("test.increment"));

	ASSERT_TRUE(result);
	EXPECT_EQ(*value, 3);
	EXPECT_TRUE(result->undoable);
	EXPECT_TRUE(result->pushedToUndoStack);
	EXPECT_EQ(undoStack->GetUndoDepth(), 1u);

	ASSERT_TRUE(undoStack->Undo());
	EXPECT_EQ(*value, 0);

	ASSERT_TRUE(undoStack->Redo());
	EXPECT_EQ(*value, 3);
}

TEST(CommandRegistryTests, CapabilityFailureReturnsStructuredError)
{
	auto value = DefectStudio::CreateRef<int>(0);
	auto capabilityService = DefectStudio::CreateRef<DefectStudio::CapabilityService>();
	capabilityService->RegisterCapability(DefectStudio::CapabilityEntry{
		"runtime.python",
		DefectStudio::CapabilityCategory::RuntimeDetected,
		false,
		"Python runtime"});
	DefectStudio::CommandRegistry registry(DefectStudio::CreateWeakRef(capabilityService));

	ASSERT_TRUE(registry.Register(
		DefectStudio::CommandMeta{
			DefectStudio::CommandID("script.run"),
			"Run Script",
			"Scripting",
			"Run a Python script",
			{DefectStudio::CapabilityID("runtime.python")},
			DefectStudio::CommandFlags::None},
		[value](DefectStudio::CommandContext &) {
			return std::make_unique<IncrementCommand>(value, 1);
		}));

	auto result = registry.Execute(DefectStudio::CommandID("script.run"));

	ASSERT_FALSE(result);
	EXPECT_EQ(result.Error().category, DefectStudio::ErrorCategory::Capability);
	EXPECT_EQ(result.Error().code, "command.capability.unavailable");
	EXPECT_EQ(*value, 0);
}

TEST(CommandRegistryTests, ObserversReceiveStartedAndFailedEvents)
{
	DefectStudio::CommandRegistry registry;
	std::vector<DefectStudio::CommandExecutionState> states;
	const std::size_t observerId = registry.AddObserver([&states](const DefectStudio::CommandExecutionEvent &event) {
		states.push_back(event.state);
	});
	ASSERT_NE(observerId, 0u);

	ASSERT_TRUE(registry.Register(
		DefectStudio::CommandMeta{
			DefectStudio::CommandID("test.fail"),
			"Fail",
			"Test",
			"Fail command",
			{},
			DefectStudio::CommandFlags::None},
		[](DefectStudio::CommandContext &) {
			return std::make_unique<FailingCommand>();
		}));

	auto result = registry.Execute(DefectStudio::CommandID("test.fail"));

	ASSERT_FALSE(result);
	ASSERT_EQ(states.size(), 2u);
	EXPECT_EQ(states[0], DefectStudio::CommandExecutionState::Started);
	EXPECT_EQ(states[1], DefectStudio::CommandExecutionState::Failed);
}
