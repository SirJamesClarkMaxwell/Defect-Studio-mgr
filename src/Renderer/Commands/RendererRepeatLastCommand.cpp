#include "Core/dspch.hpp"

#include "Renderer/Commands/RendererRepeatLastCommand.hpp"

#include "Renderer/RendererLayer.hpp"

namespace DefectStudio
{
	namespace
	{
		class RepeatLastTransformRequestCommand final : public ICommand
		{
		public:
			explicit RepeatLastTransformRequestCommand(WeakRef<RendererLayer> renderer) : m_Renderer(std::move(renderer)) {}

			Result<void> Execute(CommandContext &context) override
			{
				if (const auto renderer = m_Renderer.lock())
				{
					const auto id = context.TryGet<std::string>("renderer.window_id");
					const auto &windowId = id != nullptr ? *id : renderer->GetFocusedViewportWindowId();
					for (auto &window : renderer->GetWindows())
						if (window.windowId == windowId && !window.modalTransform && !window.pathEdit.IsActive())
							window.repeatLastTransformRequested = true;
				}
				return {};
			}

			std::string Description() const override { return "Repeat last confirmed transform"; }

		private:
			WeakRef<RendererLayer> m_Renderer;
		};
	} // namespace

	Unique<ICommand> CreateRepeatLastTransformRequestCommand(WeakRef<RendererLayer> renderer)
	{
		return CreateUnique<RepeatLastTransformRequestCommand>(std::move(renderer));
	}
} // namespace DefectStudio
