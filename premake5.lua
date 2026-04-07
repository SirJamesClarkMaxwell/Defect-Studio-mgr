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
        "src"
    }

    filter { "system:windows", "action:vs2022" }
        systemversion "latest"
        defines { "DS_PLATFORM_WINDOWS" }
        files { "assets/icon.rc" }
        pchheader "Core/dspch.hpp"
        pchsource "src/Core/dspch.cpp"

    filter "system:linux"
        pic "On"
        defines { "DS_PLATFORM_LINUX" }
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
