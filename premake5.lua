workspace "DefectStudio"
    architecture "x86_64"
    startproject "DefectStudio"
    location "build/generated/%{_ACTION}"

    configurations {
        "Debug",
        "Release",
        "Dist"
    }

outputdir = "%{cfg.buildcfg}-%{cfg.system}-%{cfg.architecture}"

-- Icon paths: absolute for Windows (VS2022 postbuild), relative for Linux (gmake2)
-- On Windows, translate to backslashes for cmd.exe
local windowsIcon = path.translate(path.getabsolute("assets/icon.png"), "\\")
local linuxIcon = "../../../assets/icon.png"

include "Vendor/GLFW"
include "Vendor/GLAD"
include "Vendor/ImGui"
include "Vendor/Tracy"

project "DefectStudio"
    location "build/generated/%{_ACTION}"
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++latest"
    staticruntime "off"
    warnings "Extra"
    flags { "MultiProcessorCompile" }

    targetdir ("build/bin/" .. outputdir .. "/%{prj.name}")
    objdir ("build/bin-int/" .. outputdir .. "/%{prj.name}")

    files {
        "src/**.hpp",
        "src/**.cpp"
    }

    includedirs {
        "src",
        "Vendor/spdlog/include",
        "Vendor/Tracy/public",
        "Vendor/GLFW/include",
        "Vendor/GLAD/generated/include",
        "Vendor/ImGui",
        "Vendor/ImGui/backends"
    }

    defines {
        "GLFW_INCLUDE_NONE",
        "IMGUI_IMPL_OPENGL_LOADER_GLAD"
    }

    links {
        "GLFW",
        "GLAD",
        "ImGui"
    }

    filter "system:windows"
        links { "opengl32", "dwmapi", "gdi32", "user32", "shell32" }

    filter { "system:windows", "action:vs2022" }
        kind "WindowedApp"
        entrypoint "mainCRTStartup"
        systemversion "latest"
        buildoptions { "/utf-8" }
        defines {
            "DS_PLATFORM_WINDOWS",
            "TRACY_ENABLE"
        }
        files { "assets/icon.rc" }
        links { "Tracy" }
        pchheader "Core/dspch.hpp"
        pchsource "src/Core/dspch.cpp"

    filter "system:linux"
        pic "On"
        defines { "DS_PLATFORM_LINUX" }
        links { "GL", "dl", "pthread", "X11", "Xrandr", "Xi", "Xxf86vm", "Xinerama", "Xcursor" }
        -- Disable -MD -MP for gmake2 on Linux to avoid absolute path issues in .d files
        filter { "system:linux", "action:gmake2" }
            buildoptions { "-fPIC" }  -- Replaces -MD -MP
        filter {}
        -- Linux does not use .rc resources; copy PNG icon for desktop integration.
        if os.host() == "windows" then
            postbuildcommands {
                'if exist "' .. windowsIcon .. '" copy /Y "' .. windowsIcon .. '" "%{cfg.targetdir}\\icon.png" >NUL'
            }
        else
            postbuildcommands {
                'if [ -f "' .. linuxIcon .. '" ]; then cp "' .. linuxIcon .. '" "%{cfg.targetdir}/icon.png"; fi'
            }
        end

    filter "configurations:Debug"
        defines { "DS_DEBUG" }
        symbols "On"

    filter "configurations:Release"
        defines { "DS_RELEASE" }
        optimize "On"

    filter "configurations:Dist"
        defines { "DS_DIST" }
        optimize "Full"

    filter {}
