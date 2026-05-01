#include <gtest/gtest.h>

#include <memory>

#include "Core/Commands/CommandPalette.hpp"

namespace
{
	class PaletteCountCommand final : public DefectStudio::ICommand
	{
	public:
		explicit PaletteCountCommand(std::shared_ptr<int> counter)
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
			return "Palette Count";
		}

	private:
		std::shared_ptr<int> m_Counter;
	};
} // namespace

TEST(CommandPaletteTests, SearchHidesCommandsMarkedHidden)
{
	DefectStudio::CommandRegistry registry;
	ASSERT_TRUE(registry.Register(
		DefectStudio::CommandMeta{
			DefectStudio::CommandID("visible.command"),
			"Visible Command",
			"General",
			"Visible in palette",
			{},
			DefectStudio::CommandFlags::None},
		[](DefectStudio::CommandContext &) {
			return std::make_unique<PaletteCountCommand>(std::make_shared<int>(0));
		}));
	ASSERT_TRUE(registry.Register(
		DefectStudio::CommandMeta{
			DefectStudio::CommandID("hidden.command"),
			"Hidden Command",
			"General",
			"Hidden in palette",
			{},
			DefectStudio::CommandFlags::HiddenFromPalette},
		[](DefectStudio::CommandContext &) {
			return std::make_unique<PaletteCountCommand>(std::make_shared<int>(0));
		}));

	DefectStudio::CommandPaletteIndex palette(registry);
	const auto items = palette.Search();

	ASSERT_EQ(items.size(), 1u);
	EXPECT_EQ(items.front().id.value, "visible.command");
}

TEST(CommandPaletteTests, SearchReturnsShortcutFromActiveKeymap)
{
	DefectStudio::CommandRegistry registry;
	DefectStudio::KeymapResolver resolver;
	DefectStudio::ContextManager contexts;
	contexts.SetActive("editor");

	ASSERT_TRUE(registry.Register(
		DefectStudio::CommandMeta{
			DefectStudio::CommandID("project.save"),
			"Save Project",
			"Project",
			"Persist project",
			{},
			DefectStudio::CommandFlags::None},
		[](DefectStudio::CommandContext &) {
			return std::make_unique<PaletteCountCommand>(std::make_shared<int>(0));
		}));
	ASSERT_TRUE(resolver.RegisterBinding(DefectStudio::KeyBinding{
		"save.shortcut",
		DefectStudio::KeyChord{DefectStudio::KeyCode::S, DefectStudio::KeyModifiers::Ctrl},
		DefectStudio::CommandID("project.save"),
		DefectStudio::ContextExpr("editor"),
		DefectStudio::KeymapLayer::Global,
		true}));

	DefectStudio::CommandPaletteIndex palette(registry);
	palette.SetKeymapResolver(&resolver, &contexts);
	const auto items = palette.Search("save");

	ASSERT_EQ(items.size(), 1u);
	EXPECT_EQ(items.front().shortcut, "Ctrl+S");
}

TEST(CommandPaletteTests, ExecuteRecordsRecentCommands)
{
	auto counter = std::make_shared<int>(0);
	DefectStudio::CommandRegistry registry;
	ASSERT_TRUE(registry.Register(
		DefectStudio::CommandMeta{
			DefectStudio::CommandID("palette.count"),
			"Count",
			"General",
			"Increment counter",
			{},
			DefectStudio::CommandFlags::None},
		[counter](DefectStudio::CommandContext &) {
			return std::make_unique<PaletteCountCommand>(counter);
		}));

	DefectStudio::CommandPaletteIndex palette(registry);
	auto result = palette.Execute(DefectStudio::CommandID("palette.count"));

	ASSERT_TRUE(result);
	EXPECT_EQ(*counter, 1);
	const auto recent = palette.GetRecentCommands();
	ASSERT_EQ(recent.size(), 1u);
	EXPECT_EQ(recent.front().value, "palette.count");
}
