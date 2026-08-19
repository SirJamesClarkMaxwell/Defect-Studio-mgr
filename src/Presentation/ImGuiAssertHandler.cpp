#include "Core/dspch.hpp"

#include "Core/Logging/Logger.hpp"

// Definition for the IM_ASSERT override declared in Presentation/ImGuiUserConfig.hpp - see the
// comment there for why this exists (avoids __debugbreak()-under-a-debugger on routine ImGui
// preconditions). Only called in Debug builds (Release/Dist never define IMGUI_USER_CONFIG, see
// premake5.lua), but always compiled - harmless if unreferenced.
void DefectStudio_LogImGuiAssertFailure(const char *expr, const char *file, int line)
{
	DS_LOG_ERROR("ImGui assertion failed: {} ({}:{})", expr, file, line);
}
