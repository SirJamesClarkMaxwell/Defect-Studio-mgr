#pragma once

namespace DefectStudio
{
	struct RendererGlobalRenderSettings;
	struct RendererWindowState;

	void DrawSelectedLabelProperties(RendererWindowState &windowState);
	void DrawAllLabelRows(RendererWindowState &windowState);
	void DrawSelectedScenePathSection(RendererWindowState &windowState);
} // namespace DefectStudio
