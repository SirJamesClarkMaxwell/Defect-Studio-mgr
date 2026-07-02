#pragma once

#include "Renderer/RendererConfig.hpp"

namespace YAML
{
	class Emitter;
}

namespace DefectStudio::ConfigYaml
{
	void EmitRendererConfig(YAML::Emitter &out, const RendererConfig &renderer);
} // namespace DefectStudio::ConfigYaml
