#pragma once

namespace DefectStudio
{
	struct RendererWindowState;
	class SceneRegistry;

	// A view-modifier mutates selection/visibility on the *live* scene (as opposed to
	// restoreViewSnapshot, which replays a captured RendererViewSnapshot). Hide/show callers record
	// visibility on the global undo stack; selection inversion remains part of the per-window
	// view-undo history.
	// TODO(T07.5.1): once modifiers are persisted with the project, give these a stable
	// TypeId()/serialize() pair instead of relying on the class hierarchy alone.
	class IViewModifier
	{
	public:
		virtual ~IViewModifier() = default;
		virtual void Apply(SceneRegistry &scene, RendererWindowState &windowState) const = 0;
	};

	class HideSelectionModifier final : public IViewModifier
	{
	public:
		void Apply(SceneRegistry &scene, RendererWindowState &windowState) const override;
	};

	class ShowAllModifier final : public IViewModifier
	{
	public:
		void Apply(SceneRegistry &scene, RendererWindowState &windowState) const override;
	};

	class InvertSelectionModifier final : public IViewModifier
	{
	public:
		void Apply(SceneRegistry &scene, RendererWindowState &windowState) const override;
	};
} // namespace DefectStudio
