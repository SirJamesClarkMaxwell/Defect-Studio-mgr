#pragma once

// Dear ImGui config override, wired in via IMGUI_USER_CONFIG (premake5.lua) rather than editing
// Vendor/imgui/imconfig.h directly - imconfig.h lives inside the ImGui git submodule, and
// `git submodule update --force` (run by scripts/Windows/GenerateProjects.bat) silently discards
// uncommitted edits there. This file lives in the main repo instead, so it survives.
//
// Log-and-continue instead of assert()'s default __debugbreak()-under-a-debugger behavior (VS
// shows "A breakpoint instruction was executed" and pauses, indistinguishable from a real crash,
// for what is often just Dear ImGui defensively checking a precondition). Implemented in
// Presentation/ImGuiAssertHandler.cpp where Logger.hpp is cheap to include. premake5.lua only
// defines IMGUI_USER_CONFIG (which pulls in this file) for the Debug configuration, so
// Release/Dist keep plain assert() (a no-op under NDEBUG, same as upstream default) unchanged.
// ponytail: silences real bugs into log lines instead of a hard stop - revisit if silent ImGui
// misuse ever causes a visible rendering bug instead of just a log line.
void DefectStudio_LogImGuiAssertFailure(const char *expr, const char *file, int line);
#define IM_ASSERT(_EXPR) do { if (!(_EXPR)) DefectStudio_LogImGuiAssertFailure(#_EXPR, __FILE__, __LINE__); } while (0)
