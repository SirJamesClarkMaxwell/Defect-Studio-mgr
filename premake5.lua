workspace "DefectStudio"
    architecture "x86_64"
    startproject "DefectStudio"
    location "build/generated/%{_ACTION}"

    configurations {
        "Debug",
        "Release",
        "Dist"
    }

if _ACTION == "gmake2" then
    outputdir = "%{cfg.buildcfg}-%{cfg.system}-%{cfg.architecture}-gmake2"
else
    outputdir = "%{cfg.buildcfg}-%{cfg.system}-%{cfg.architecture}"
end
_DS_ROOT = path.getabsolute(".")

-- Icon paths: absolute for Windows (VS2022 postbuild), relative for Linux (gmake2)
-- On Windows, translate to backslashes for cmd.exe
local windowsIcon = path.translate(path.getabsolute("install/app/assets/icon.png"), "\\")
local linuxIcon = "../../../install/app/assets/icon.png"

local function appendUnique(list, value)
    if value == nil or value == "" then
        return
    end

    for _, existing in ipairs(list) do
        if existing == value then
            return
        end
    end

    table.insert(list, value)
end

local function readVenvHome(venvCfgPath)
    local file = io.open(venvCfgPath, "r")
    if file == nil then
        return nil
    end

    for line in file:lines() do
        local home = line:match("^home%s*=%s*(.+)$")
        if home ~= nil and home ~= "" then
            file:close()
            return home
        end
    end

    file:close()
    return nil
end

local pythonIncludeDir = nil
local pythonLibDir = nil
local pythonLibName = nil

local pythonRuntimeRoots = {}
if os.host() == "windows" then
    appendUnique(pythonRuntimeRoots, path.getabsolute("install/app/python/windows"))
elseif os.host() == "linux" then
    appendUnique(pythonRuntimeRoots, path.getabsolute("install/app/python/linux"))
elseif os.host() == "macosx" then
    appendUnique(pythonRuntimeRoots, path.getabsolute("install/app/python/macos"))
end
appendUnique(pythonRuntimeRoots, path.getabsolute("install/app/python"))
appendUnique(pythonRuntimeRoots, path.getabsolute(".venv"))

local venvHome = readVenvHome(path.getabsolute(".venv/pyvenv.cfg"))
if venvHome ~= nil then
    appendUnique(pythonRuntimeRoots, path.getabsolute(venvHome))
end

local pythonIncludeCandidates = {}
local pythonLibDirCandidates = {}
for _, runtimeRoot in ipairs(pythonRuntimeRoots) do
    appendUnique(pythonIncludeCandidates, runtimeRoot .. "/Include")
    appendUnique(pythonIncludeCandidates, runtimeRoot .. "/include")
    appendUnique(pythonLibDirCandidates, runtimeRoot .. "/libs")
    appendUnique(pythonLibDirCandidates, runtimeRoot .. "/lib")
end

for _, candidate in ipairs(pythonIncludeCandidates) do
    if os.isdir(candidate) then
        pythonIncludeDir = candidate
        break
    end
end

local pythonLibPatterns = {}
if os.host() == "windows" then
    pythonLibPatterns = { "python3*.lib", "python*.lib" }
else
    pythonLibPatterns = { "libpython3*.so", "libpython3*.a", "libpython*.so", "libpython*.a" }
end

for _, libDirCandidate in ipairs(pythonLibDirCandidates) do
    if os.isdir(libDirCandidate) then
        for _, pattern in ipairs(pythonLibPatterns) do
            local matches = os.matchfiles(libDirCandidate .. "/" .. pattern)
            if #matches > 0 then
                pythonLibDir = libDirCandidate
                pythonLibName = path.getbasename(matches[1])
                if os.host() ~= "windows" then
                    pythonLibName = pythonLibName:gsub("^lib", "")
                end
                break
            end
        end
    end

    if pythonLibName ~= nil then
        break
    end
end

_DS_PYTHON_INCLUDE_DIR = pythonIncludeDir
_DS_PYTHON_LIB_DIR = pythonLibDir
_DS_PYTHON_LIB_NAME = pythonLibName
_DS_PYTHON_EMBED_AVAILABLE = pythonIncludeDir ~= nil and pythonLibDir ~= nil and pythonLibName ~= nil

group "Dependencies"
include "Vendor/GLFW"
include "Vendor/GLAD"
include "Vendor/ImGui"
dofile "Vendor/thread-pool/premake5.lua"
dofile "Vendor/json/premake5.lua"
dofile "Vendor/yaml-cpp/premake5.lua"
include "Vendor/ImGuiNotify"

dofile "Vendor/Tracy/premake5.lua"
dofile "Vendor/GoogleTest/GoogleTest.premake5.lua"
dofile "Vendor/nanobind.premake5.lua"
group ""

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

    removefiles {
        "src/ScientificRuntime/PythonBindings/**.hpp",
        "src/ScientificRuntime/PythonBindings/**.cpp"
    }

    vpaths {
        ["App/*"] = { "src/App/**.cpp", "src/App/**.hpp" },
        ["Core/*"] = { "src/Core/**.cpp", "src/Core/**.hpp" },
        ["Debug/*"] = { "src/Debug/**.cpp", "src/Debug/**.hpp" },
        ["Demo/*"] = { "src/Demo/**.cpp", "src/Demo/**.hpp" },
        ["Domain/*"] = { "src/Domain/**.cpp", "src/Domain/**.hpp" },
        ["IO/*"] = { "src/IO/**.cpp", "src/IO/**.hpp" },
        ["Presentation/*"] = { "src/Presentation/**.cpp", "src/Presentation/**.hpp" },
        ["ScientificRuntime/*"] = { "src/ScientificRuntime/**.cpp", "src/ScientificRuntime/**.hpp" },
        ["Storage/*"] = { "src/Storage/**.cpp", "src/Storage/**.hpp" },
    }

    includedirs {
        "src",
        "Vendor/spdlog/include",
        "Vendor/Tracy/public",
        "Vendor/GLFW/include",
        "Vendor/GLAD/generated/include",
        "Vendor/ImGui",
        "Vendor/ImGui/backends",
        "Vendor/imgui-command-palette",
        "Vendor/ImGuiNotify/win32Example/backends",
        "install/app/assets/fonts",
        "Vendor/thread-pool/include",
        "Vendor/json/include",
        "Vendor/yaml-cpp/include"
    }

    if _DS_PYTHON_EMBED_AVAILABLE then
        includedirs {
            _DS_PYTHON_INCLUDE_DIR,
            "Vendor/nanobind/include",
            "Vendor/nanobind/ext/robin_map/include"
        }
    end

    defines {
        "GLFW_INCLUDE_NONE",
        "IMGUI_IMPL_OPENGL_LOADER_GLAD",
        "YAML_CPP_STATIC_DEFINE"
    }

    if _DS_PYTHON_EMBED_AVAILABLE then
        defines { "DS_PYTHON_CAPI_AVAILABLE=1" }
        libdirs { _DS_PYTHON_LIB_DIR }
        links {
            _DS_PYTHON_LIB_NAME,
            "nanobind"
        }
    else
        defines { "DS_PYTHON_CAPI_AVAILABLE=0" }
    end

    links {
        "GLFW",
        "GLAD",
        "ImGui",
        "yaml-cpp"
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
        files { "install/app/assets/icon.rc" }
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

    filter "system:macosx"
        defines { "DS_PLATFORM_MACOS" }

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

project "DefectStudioTests"
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
        "tests/**.hpp",
        "tests/**.cpp",
        "Vendor/GoogleTest/googletest/src/gtest_main.cc",


        "src/Core/**.hpp",
        "src/Core/**.cpp",

        "src/App/**.hpp",
        "src/App/**.cpp",
        
        "src/Debug/**.hpp",
        "src/Debug/**.cpp",

        "src/Demo/**.hpp",
        "src/Demo/**.cpp",

        "src/Domain/**.hpp",
        "src/Domain/**.cpp",

        "src/IO/**.hpp",
        "src/IO/**.cpp",

        "src/ScientificRuntime/**.hpp",
        "src/ScientificRuntime/**.cpp",

        "src/Storage/**.hpp",
        "src/Storage/**.cpp",

        "src/Presentation/**.hpp",
        "src/Presentation/**.cpp",
    }

    removefiles {
        "src/Core/dspch.cpp",
        "src/ScientificRuntime/PythonBindings/**.hpp",
        "src/ScientificRuntime/PythonBindings/**.cpp"
    }

    vpaths {
        ["Tests/*"] = { "tests/**.cpp", "tests/**.hpp" },
        ["App/*"] = { "src/App/**.cpp", "src/App/**.hpp" },
        ["Core/*"] = { "src/Core/**.cpp", "src/Core/**.hpp" },
        ["Debug/*"] = { "src/Debug/**.cpp", "src/Debug/**.hpp" },
        ["Demo/*"] = { "src/Demo/**.cpp", "src/Demo/**.hpp" },
        ["Domain/*"] = { "src/Domain/**.cpp", "src/Domain/**.hpp" },
        ["IO/*"] = { "src/IO/**.cpp", "src/IO/**.hpp" },
        ["Presentation/*"] = { "src/Presentation/**.cpp", "src/Presentation/**.hpp" },
        ["ScientificRuntime/*"] = { "src/ScientificRuntime/**.cpp", "src/ScientificRuntime/**.hpp" },
        ["Storage/*"] = { "src/Storage/**.cpp", "src/Storage/**.hpp" },
    }

    includedirs {
        "src",
        "Vendor/spdlog/include",
        "Vendor/Tracy/public",
        "Vendor/GoogleTest/googletest/include",
        "Vendor/GoogleTest/googlemock/include",
        "Vendor/GLFW/include",
        "Vendor/GLAD/generated/include",
        "Vendor/ImGui",
        "Vendor/ImGui/backends",
        "Vendor/imgui-command-palette",
        "Vendor/ImGuiNotify/win32Example/backends",
        "install/app/assets/fonts",
        "Vendor/thread-pool/include",
        "Vendor/json/include",
        "Vendor/yaml-cpp/include"
    }

    if _DS_PYTHON_EMBED_AVAILABLE then
        includedirs {
            _DS_PYTHON_INCLUDE_DIR,
            "Vendor/nanobind/include",
            "Vendor/nanobind/ext/robin_map/include"
        }
    end

    if _DS_PYTHON_EMBED_AVAILABLE then
        defines { "DS_PYTHON_CAPI_AVAILABLE=1" }
    else
        defines { "DS_PYTHON_CAPI_AVAILABLE=0" }
    end

    links {
        "GoogleTest",
        "GLFW",
        "GLAD",
        "ImGui",
        "yaml-cpp"
    }

    if _DS_PYTHON_EMBED_AVAILABLE then
        libdirs { _DS_PYTHON_LIB_DIR }
        links {
            _DS_PYTHON_LIB_NAME,
            "nanobind"
        }
    end

    filter "system:windows"
        links { "opengl32", "dwmapi", "gdi32", "user32", "shell32" }
        systemversion "latest"
        defines {
            "DS_PLATFORM_WINDOWS",
            "YAML_CPP_STATIC_DEFINE",
            "GLFW_INCLUDE_NONE",
            "IMGUI_IMPL_OPENGL_LOADER_GLAD"
        }

    filter { "system:windows", "action:vs2022" }
        buildoptions { "/utf-8" }

    filter "system:linux"
        pic "On"
        defines { "DS_PLATFORM_LINUX" }
        links { "pthread" }

    filter "system:macosx"
        defines { "DS_PLATFORM_MACOS" }

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
