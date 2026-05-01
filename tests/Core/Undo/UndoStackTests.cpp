#include <gtest/gtest.h>

#include <memory>

#include "Core/Undo/UndoStack.hpp"

namespace
{
	class AddCommand final : public DefectStudio::ICommand
	{
	public:
		AddCommand(std::shared_ptr<int> value, int amount)
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
			return "Add";
		}

		bool IsUndoable() const noexcept override
		{
			return true;
		}

		bool CanMerge(const DefectStudio::ICommand &next) const noexcept override
		{
			const auto *other = dynamic_cast<const AddCommand *>(&next);
			return other != nullptr && other->m_Value == m_Value;
		}

		DefectStudio::Result<void> Merge(std::unique_ptr<DefectStudio::ICommand> next) override
		{
			auto *other = dynamic_cast<AddCommand *>(next.get());
			if (other == nullptr)
				return DefectStudio::ICommand::Merge(std::move(next));

			m_Amount += other->m_Amount;
			return {};
		}

	private:
		std::shared_ptr<int> m_Value;
		int m_Amount = 0;
	};
} // namespace

TEST(UndoStackTests, UndoAndRedoMoveHistoryIndex)
{
	auto value = std::make_shared<int>(0);
	DefectStudio::UndoStack stack;
	DefectStudio::CommandContext context;

	auto command = std::make_unique<AddCommand>(value, 5);
	ASSERT_TRUE(command->Execute(context));
	ASSERT_TRUE(stack.PushExecuted(std::move(command)));

	EXPECT_EQ(*value, 5);
	EXPECT_TRUE(stack.CanUndo());
	EXPECT_FALSE(stack.CanRedo());

	ASSERT_TRUE(stack.Undo());
	EXPECT_EQ(*value, 0);
	EXPECT_FALSE(stack.CanUndo());
	EXPECT_TRUE(stack.CanRedo());

	ASSERT_TRUE(stack.Redo());
	EXPECT_EQ(*value, 5);
}

TEST(UndoStackTests, AdjacentMergeableCommandsBecomeOneUndoRecord)
{
	auto value = std::make_shared<int>(0);
	DefectStudio::UndoStack stack;
	DefectStudio::CommandContext context;

	auto first = std::make_unique<AddCommand>(value, 2);
	ASSERT_TRUE(first->Execute(context));
	ASSERT_TRUE(stack.PushExecuted(std::move(first)));

	auto second = std::make_unique<AddCommand>(value, 3);
	ASSERT_TRUE(second->Execute(context));
	ASSERT_TRUE(stack.PushExecuted(std::move(second)));

	EXPECT_EQ(*value, 5);
	EXPECT_EQ(stack.GetUndoDepth(), 1u);

	ASSERT_TRUE(stack.Undo());
	EXPECT_EQ(*value, 0);
}

TEST(UndoStackTests, GroupCommitsAsSingleUndoRecord)
{
	auto value = std::make_shared<int>(0);
	DefectStudio::UndoStack stack;
	DefectStudio::CommandContext context;

	ASSERT_TRUE(stack.BeginGroup("Batch Add"));
	for (int amount : {1, 2, 3})
	{
		auto command = std::make_unique<AddCommand>(value, amount);
		ASSERT_TRUE(command->Execute(context));
		ASSERT_TRUE(stack.PushExecuted(std::move(command)));
	}
	ASSERT_TRUE(stack.EndGroup());

	EXPECT_EQ(*value, 6);
	EXPECT_EQ(stack.GetUndoDepth(), 1u);
	EXPECT_EQ(stack.GetUndoDescription(), "Batch Add");

	ASSERT_TRUE(stack.Undo());
	EXPECT_EQ(*value, 0);
}

TEST(UndoStackTests, CleanStateTracksUndoRedoIndex)
{
	auto value = std::make_shared<int>(0);
	DefectStudio::UndoStack stack;
	DefectStudio::CommandContext context;

	auto command = std::make_unique<AddCommand>(value, 4);
	ASSERT_TRUE(command->Execute(context));
	ASSERT_TRUE(stack.PushExecuted(std::move(command)));
	stack.MarkClean();
	EXPECT_TRUE(stack.IsClean());

	ASSERT_TRUE(stack.Undo());
	EXPECT_FALSE(stack.IsClean());

	ASSERT_TRUE(stack.Redo());
	EXPECT_TRUE(stack.IsClean());
}
