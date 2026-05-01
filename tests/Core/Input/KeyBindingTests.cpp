#include <gtest/gtest.h>

#include <memory>

#include "Core/Input/KeyBinding.hpp"

namespace
{
	class CountCommand final : public DefectStudio::ICommand
	{
	public:
		explicit CountCommand(std::shared_ptr<int> counter)
			: m_Counter(std::move(counter))
		{
		}

		DefectStudio::Result<void> Execute(DefectStudio::CommandContext &context) override
		{
			(void)context;
			++(*m_Counter);
			return {};
		}

		std::string Description() const override
		{
			return "Count";
		}

	private:
		std::shared_ptr<int> m_Counter;
	};

	DefectStudio::KeyChord Ctrl(DefectStudio::KeyCode key)
	{
		return DefectStudio::KeyChord{key, DefectStudio::KeyModifiers::Ctrl};
	}
} // namespace

TEST(KeyBindingTests, ContextExpressionSupportsConjunctionAndNegation)
{
	DefectStudio::ContextManager contexts;
	contexts.SetActive("editor");
	contexts.SetActive("selection");

	EXPECT_TRUE(DefectStudio::ContextExpr("editor && selection").Matches(contexts));
	EXPECT_TRUE(DefectStudio::ContextExpr("editor && !textInput").Matches(contexts));
	EXPECT_FALSE(DefectStudio::ContextExpr("editor && textInput").Matches(contexts));
}

TEST(KeyBindingTests, ResolverRejectsConflictingBindingInSameLayerAndContext)
{
	DefectStudio::KeymapResolver resolver;
	ASSERT_TRUE(resolver.RegisterBinding(DefectStudio::KeyBinding{
		"global.undo",
		Ctrl(DefectStudio::KeyCode::Z),
		DefectStudio::CommandID("edit.undo"),
		DefectStudio::ContextExpr("editor"),
		DefectStudio::KeymapLayer::Global,
		true}));

	auto conflict = resolver.RegisterBinding(DefectStudio::KeyBinding{
		"global.other",
		Ctrl(DefectStudio::KeyCode::Z),
		DefectStudio::CommandID("other"),
		DefectStudio::ContextExpr("editor"),
		DefectStudio::KeymapLayer::Global,
		true});

	ASSERT_FALSE(conflict);
	EXPECT_EQ(conflict.Error().code, "keymap.register.conflict");
	ASSERT_EQ(resolver.GetConflicts().size(), 1u);
}

TEST(KeyBindingTests, WindowLocalBindingOverridesGlobalBinding)
{
	DefectStudio::ContextManager contexts;
	DefectStudio::KeymapResolver resolver;

	ASSERT_TRUE(resolver.RegisterBinding(DefectStudio::KeyBinding{
		"global.save",
		Ctrl(DefectStudio::KeyCode::S),
		DefectStudio::CommandID("project.save"),
		DefectStudio::ContextExpr(),
		DefectStudio::KeymapLayer::Global,
		true}));
	ASSERT_TRUE(resolver.RegisterBinding(DefectStudio::KeyBinding{
		"window.search",
		Ctrl(DefectStudio::KeyCode::S),
		DefectStudio::CommandID("panel.search"),
		DefectStudio::ContextExpr("searchPanel"),
		DefectStudio::KeymapLayer::WindowLocal,
		true}));

	contexts.SetActive("searchPanel");
	auto resolved = resolver.Resolve(Ctrl(DefectStudio::KeyCode::S), contexts);

	ASSERT_TRUE(resolved);
	EXPECT_EQ(resolved->commandId.value, "panel.search");
}

TEST(KeyBindingTests, InputProcessorExecutesResolvedCommand)
{
	auto counter = std::make_shared<int>(0);
	DefectStudio::ContextManager contexts;
	DefectStudio::KeymapResolver resolver;
	DefectStudio::CommandRegistry commands;
	DefectStudio::KeyInputProcessor processor(commands, resolver, contexts);

	ASSERT_TRUE(commands.Register(
		DefectStudio::CommandMeta{
			DefectStudio::CommandID("count"),
			"Count",
			"Test",
			"Increment counter",
			{},
			DefectStudio::CommandFlags::None},
		[counter](DefectStudio::CommandContext &) {
			return std::make_unique<CountCommand>(counter);
		}));
	ASSERT_TRUE(resolver.RegisterBinding(DefectStudio::KeyBinding{
		"count.shortcut",
		Ctrl(DefectStudio::KeyCode::K),
		DefectStudio::CommandID("count"),
		DefectStudio::ContextExpr(),
		DefectStudio::KeymapLayer::Global,
		true}));

	auto result = processor.HandleKeyPressed(Ctrl(DefectStudio::KeyCode::K));

	ASSERT_TRUE(result);
	EXPECT_TRUE(result->handled);
	ASSERT_TRUE(result->commandId);
	EXPECT_EQ(result->commandId->value, "count");
	EXPECT_EQ(*counter, 1);
}
