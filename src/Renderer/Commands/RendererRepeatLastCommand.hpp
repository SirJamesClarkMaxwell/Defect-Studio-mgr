#pragma once

#include "Core/Commands/Command.hpp"
#include "Core/Utils/Memory.hpp"

namespace DefectStudio
{
	class RendererLayer;
	[[nodiscard]] Unique<ICommand> CreateRepeatLastTransformRequestCommand(WeakRef<RendererLayer> renderer);
} // namespace DefectStudio
