#include <gtest/gtest.h>

#include "Core/Input/KeyBinding.hpp"

namespace
{
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

TEST(KeyBindingTests, InputProcessorReturnsResolvedCommand)
{
	DefectStudio::ContextManager contexts;
	DefectStudio::KeymapResolver resolver;
	DefectStudio::KeyInputProcessor processor(resolver, contexts);

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
}

TEST(KeyBindingTests, RendererBindingResolvesOnlyWhenViewportContextIsActive)
{
	DefectStudio::ContextManager contexts;
	DefectStudio::KeymapResolver resolver;
	const DefectStudio::KeyChord chord = Ctrl(DefectStudio::KeyCode::A);

	ASSERT_TRUE(resolver.RegisterBinding(DefectStudio::KeyBinding{
		"renderer.align_axis_a",
		chord,
		DefectStudio::CommandID("renderer.align_axis_a"),
		DefectStudio::ContextExpr("renderer.viewport.focused"),
		DefectStudio::KeymapLayer::WindowLocal,
		true}));

	EXPECT_FALSE(resolver.Resolve(chord, contexts));

	contexts.SetActive("renderer.viewport.focused");
	const auto resolved = resolver.Resolve(chord, contexts);

	ASSERT_TRUE(resolved);
	EXPECT_EQ(resolved->commandId.value, "renderer.align_axis_a");
	EXPECT_EQ(resolved->layer, DefectStudio::KeymapLayer::WindowLocal);
}

TEST(KeyBindingTests, KeyInputProcessorReturnsRendererCommand)
{
	DefectStudio::ContextManager contexts;
	DefectStudio::KeymapResolver resolver;
	DefectStudio::KeyInputProcessor processor(resolver, contexts);
	contexts.SetActive("renderer.viewport.focused");

	ASSERT_TRUE(resolver.RegisterBinding(DefectStudio::KeyBinding{
		"renderer.zoom_in",
		Ctrl(DefectStudio::KeyCode::Equal),
		DefectStudio::CommandID("renderer.zoom_in"),
		DefectStudio::ContextExpr("renderer.viewport.focused"),
		DefectStudio::KeymapLayer::WindowLocal,
		true}));

	const auto result = processor.HandleKeyPressed(Ctrl(DefectStudio::KeyCode::Equal));

	ASSERT_TRUE(result);
	EXPECT_TRUE(result->handled);
	ASSERT_TRUE(result->commandId);
	EXPECT_EQ(result->commandId->value, "renderer.zoom_in");
}
