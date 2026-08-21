workspace "DefectStudio"
    architecture "x86_64"
    startproject "DefectStudio"
    location "build/generated/%{_ACTION}"
    toolset "msc-v145"

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
local windowsShaderSource = path.translate(path.getabsolute("src/Renderer/OpenGl/Shaders"), "\\")
-- Deployed config/fonts/assets the running exe actually reads (keybindings.yaml etc.) - re-synced
-- on every build so editing install/ under source control never goes stale next to the binary.
local windowsInstallSource = path.translate(path.getabsolute("install"), "\\")

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

local function ApplyDependencyPaths()
    targetdir ("build/bin/" .. outputdir .. "/%{prj.name}")
    objdir ("build/bin-int/" .. outputdir .. "/%{prj.name}")
end

local function DefineYamlCppProject()
    project "yaml-cpp"
        kind "StaticLib"
        language "C++"
        cppdialect "C++17"
        staticruntime "off"
        warnings "Off"
        ApplyDependencyPaths()

        files {
            "Vendor/yaml-cpp/include/**.h",
            "Vendor/yaml-cpp/include/**.hpp",
            "Vendor/yaml-cpp/src/**.h",
            "Vendor/yaml-cpp/src/**.cpp"
        }

        includedirs {
            "Vendor/yaml-cpp/include",
            "Vendor/yaml-cpp/src"
        }

        defines { "YAML_CPP_STATIC_DEFINE" }

        filter "system:linux"
            pic "On"

        filter {}
end

local function DefineGoogleTestProjects()
    project "GoogleTest"
        kind "StaticLib"
        language "C++"
        cppdialect "C++17"
        staticruntime "off"
        warnings "Off"
        ApplyDependencyPaths()

        files {
            "Vendor/GoogleTest/googletest/include/**.h",
            "Vendor/GoogleTest/googletest/src/gtest-all.cc"
        }

        includedirs {
            "Vendor/GoogleTest/googletest",
            "Vendor/GoogleTest/googletest/include"
        }

        filter "system:windows"
            systemversion "latest"
            defines { "_CRT_SECURE_NO_WARNINGS" }

        filter "system:linux"
            pic "On"

        filter {}

    project "GoogleTestMain"
        kind "StaticLib"
        language "C++"
        cppdialect "C++17"
        staticruntime "off"
        warnings "Off"
        ApplyDependencyPaths()

        files { "Vendor/GoogleTest/googletest/src/gtest_main.cc" }

        includedirs {
            "Vendor/GoogleTest/googletest",
            "Vendor/GoogleTest/googletest/include"
        }

        links { "GoogleTest" }

        filter "system:linux"
            pic "On"

        filter {}
end

local function DefineTracyProject()
    project "Tracy"
        kind "StaticLib"
        language "C++"
        cppdialect "C++17"
        staticruntime "off"
        warnings "Off"
        ApplyDependencyPaths()

        files { "Vendor/Tracy/public/TracyClient.cpp" }
        includedirs { "Vendor/Tracy/public" }
        defines {
            "TRACY_ENABLE",
            "TRACY_NO_SYSTEM_TRACING"
        }

        filter "system:windows"
            systemversion "latest"
            defines { "_CRT_SECURE_NO_WARNINGS" }

        filter "system:linux"
            pic "On"

        filter {}
end

local function DefineNativeFileDialogProject()
    project "nfd"
        kind "StaticLib"
        language "C++"
        cppdialect "C++17"
        staticruntime "off"
        warnings "Off"
        ApplyDependencyPaths()

        files {
            "Vendor/nativefiledialog-extended/src/include/**.h",
            "Vendor/nativefiledialog-extended/src/include/**.hpp"
        }

        includedirs { "Vendor/nativefiledialog-extended/src/include" }

        filter "system:windows"
            systemversion "latest"
            files { "Vendor/nativefiledialog-extended/src/nfd_win.cpp" }
            defines { "_CRT_SECURE_NO_WARNINGS" }

        filter "system:linux"
            pic "On"
            files { "Vendor/nativefiledialog-extended/src/nfd_gtk.cpp" }

        filter "system:macosx"
            files { "Vendor/nativefiledialog-extended/src/nfd_cocoa.m" }

        filter {}
end

local function DefineImGuizmoProject()
    project "ImGuizmo"
        kind "StaticLib"
        language "C++"
        cppdialect "C++17"
        staticruntime "off"
        warnings "Off"
        ApplyDependencyPaths()

        files { "Vendor/ImGuizmo/ImGuizmo.cpp", "Vendor/ImGuizmo/ImGuizmo.h" }
        includedirs { "Vendor/ImGui" }

        filter "system:windows"
            systemversion "latest"
            defines { "_CRT_SECURE_NO_WARNINGS" }

        filter "system:linux"
            pic "On"

        filter {}
end

local function DefineImGuiColorTextEditProject()
    project "ImGuiColorTextEdit"
        kind "StaticLib"
        language "C++"
        cppdialect "C++17"
        staticruntime "off"
        warnings "Off"
        ApplyDependencyPaths()

        -- TextDiff.cpp/dtl.h are an optional diff-view feature we don't use - only the editor
        -- widget itself (TextEditor.cpp) is compiled.
        files { "Vendor/ImGuiColorTextEdit/TextEditor.cpp", "Vendor/ImGuiColorTextEdit/TextEditor.h" }
        includedirs { "Vendor/ImGui" }

        filter "system:windows"
            systemversion "latest"
            defines { "_CRT_SECURE_NO_WARNINGS" }

        filter "system:linux"
            pic "On"

        filter {}
end

local function DefineNanobindProject()
    project "nanobind"
        kind (_DS_PYTHON_EMBED_AVAILABLE and "StaticLib" or "Utility")
        language "C++"
        cppdialect "C++17"
        staticruntime "off"
        warnings "Off"
        ApplyDependencyPaths()

        includedirs {
            "Vendor/nanobind/include",
            "Vendor/nanobind/ext/robin_map/include"
        }

        defines { "Py_NO_LINK_LIB" }

        if _DS_PYTHON_EMBED_AVAILABLE then
            files { "Vendor/nanobind/src/nb_combined.cpp" }
            includedirs { _DS_PYTHON_INCLUDE_DIR }
        end

        filter "system:windows"
            systemversion "latest"
            buildoptions { "/bigobj" }

        filter { "system:windows", "configurations:Debug" }
            buildoptions { "/U_DEBUG" }

        filter "system:linux"
            pic "On"

        filter {}
end

group "Dependencies"
include "Vendor/GLFW"
include "Vendor/GLAD"
include "Vendor/ImGui"
include "Vendor/ImPlot"

-- Patch the vendored ImGui project from here rather than editing Vendor/imgui/imconfig.h: that
-- file lives inside the ImGui git submodule, and `git submodule update --force`
-- (scripts/Windows/GenerateProjects.bat) silently discards uncommitted edits there. See
-- src/Presentation/ImGuiUserConfig.hpp for what this actually overrides.
project "ImGui"
    includedirs { "src" }
    filter "configurations:Debug"
        defines { 'IMGUI_USER_CONFIG="Presentation/ImGuiUserConfig.hpp"' }
    filter {}

DefineYamlCppProject()
DefineGoogleTestProjects()
DefineTracyProject()
DefineNativeFileDialogProject()
DefineImGuizmoProject()
DefineImGuiColorTextEditProject()
DefineNanobindProject()
group ""

local function ApplyDependencyRuntimeFilters(projectName)
    project(projectName)
    filter "configurations:Debug"
        runtime "Debug"
        symbols "On"
    filter "configurations:Release"
        runtime "Release"
        optimize "On"
    filter "configurations:Dist"
        runtime "Release"
        optimize "Full"
    filter {}
end

ApplyDependencyRuntimeFilters("GLAD")
ApplyDependencyRuntimeFilters("ImGui")
ApplyDependencyRuntimeFilters("ImPlot")
ApplyDependencyRuntimeFilters("yaml-cpp")
ApplyDependencyRuntimeFilters("Tracy")
ApplyDependencyRuntimeFilters("GoogleTest")
ApplyDependencyRuntimeFilters("GoogleTestMain")
ApplyDependencyRuntimeFilters("nfd")
ApplyDependencyRuntimeFilters("ImGuizmo")
ApplyDependencyRuntimeFilters("ImGuiColorTextEdit")
ApplyDependencyRuntimeFilters("nanobind")

project "nanobind"
    defines { "Py_NO_LINK_LIB" }
filter {}

project "DefectStudioPythonBridge"
    location "build/generated/%{_ACTION}"
    kind (_DS_PYTHON_EMBED_AVAILABLE and "SharedLib" or "Utility")
    language "C++"
    cppdialect "C++latest"
    staticruntime "off"
    warnings "Extra"

    targetdir ("build/bin/" .. outputdir .. "/%{prj.name}")
    objdir ("build/bin-int/" .. outputdir .. "/%{prj.name}")
    targetname "ds_python_bridge"

    if _DS_PYTHON_EMBED_AVAILABLE then
        files { "src/ScientificRuntime/PythonBindings/**.cpp" }
        includedirs {
            "src",
            _DS_PYTHON_INCLUDE_DIR,
            "Vendor/nanobind/include",
            "Vendor/nanobind/ext/robin_map/include"
        }
        libdirs { _DS_PYTHON_LIB_DIR }
        links {
            _DS_PYTHON_LIB_NAME,
            "nanobind"
        }
        defines { "Py_NO_LINK_LIB" }
    end

    filter "system:windows"
        systemversion "latest"
        targetextension ".pyd"

    filter { "system:windows", "configurations:Debug" }
        buildoptions { "/U_DEBUG" }
        linkoptions {
            "/NODEFAULTLIB:python312_d.lib",
            "/NODEFAULTLIB:python313_d.lib",
            "/NODEFAULTLIB:python314_d.lib",
            "/NODEFAULTLIB:python312t_d.lib",
            "/NODEFAULTLIB:python313t_d.lib",
            "/NODEFAULTLIB:python314t_d.lib"
        }

    filter "system:linux"
        pic "On"
        targetextension ".so"

    filter "configurations:Debug"
        symbols "On"

    filter "configurations:Release"
        optimize "On"

    filter "configurations:Dist"
        optimize "Full"

filter {}

project "DefectStudio"
    location "build/generated/%{_ACTION}"
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++latest"
    staticruntime "off"
    warnings "Extra"
    flags { "MultiProcessorCompile" }
    -- entt (Vendor/entt) relies on C++20 "implicit typename in dependent contexts" (P0634),
    -- which MSVC only honors under /permissive- (its default non-conforming parser doesn't apply
    -- it even with cppdialect "C++latest" - std-version and conformance-mode are separate MSVC
    -- flags). Without this, Vendor/entt/src/entt/core/hashed_string.hpp fails to compile.
    filter "toolset:msc*"
        buildoptions { "/permissive-" }
    filter {}

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
        ["Renderer/*"] = { "src/Renderer/**.cpp", "src/Renderer/**.hpp" },
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
        "Vendor/ImPlot",
        "Vendor/ImGuizmo",
        "Vendor/ImGuiColorTextEdit",
        "Vendor/imgui-command-palette",
        "Vendor/ImGuiNotify/win32Example/backends",
        "Vendor/stb",
        "Vendor/glm",
        "Vendor/entt/src",
        "Vendor/nativefiledialog-extended/src/include",
        "install/app/assets/fonts",
        "Vendor/thread-pool/include",
        "Vendor/json/include",
        "Vendor/yaml-cpp/include",
        "Vendor/stduuid/include"
    }

    defines {
        "GLFW_INCLUDE_NONE",
        "IMGUI_IMPL_OPENGL_LOADER_GLAD",
        "YAML_CPP_STATIC_DEFINE",
        "DS_PYTHON_CAPI_AVAILABLE=0"
    }

    links {
        "GLFW",
        "GLAD",
        "ImGui",
        "ImPlot",
        "ImGuizmo",
        "ImGuiColorTextEdit",
        "nfd",
        "yaml-cpp"
    }

    filter "system:windows"
        links { "opengl32", "dwmapi", "gdi32", "user32", "shell32" }
        postbuildcommands {
            'if not exist "%{cfg.targetdir}\\shaders" mkdir "%{cfg.targetdir}\\shaders"',
            'xcopy /E /Y /I "' .. windowsShaderSource .. '\\*" "%{cfg.targetdir}\\shaders\\" >NUL',
            -- /D: only copies files newer than the destination, so repeat builds stay fast.
            'if not exist "%{cfg.targetdir}\\install" mkdir "%{cfg.targetdir}\\install"',
            'xcopy /E /Y /I /D "' .. windowsInstallSource .. '\\*" "%{cfg.targetdir}\\install\\" >NUL'
        }

    filter { "system:windows", "action:vs2022" }
        kind "WindowedApp"
        entrypoint "mainCRTStartup"
        systemversion "latest"
        buildoptions { "/utf-8" }
        defines {
            "DS_PLATFORM_WINDOWS",
            "TRACY_ENABLE",
            "TRACY_NO_SYSTEM_TRACING"
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
        filter "system:linux"
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
        postbuildcommands {
                'mkdir -p "%{cfg.targetdir}/shaders"',
                'cp -r src/Renderer/OpenGl/Shaders/. "%{cfg.targetdir}/shaders/"',
                'mkdir -p "%{cfg.targetdir}/install"',
                'cp -r install/. "%{cfg.targetdir}/install/"'
        }

    filter "system:macosx"
        defines { "DS_PLATFORM_MACOS" }

    filter "configurations:Debug"
        defines { "DS_DEBUG", 'IMGUI_USER_CONFIG="Presentation/ImGuiUserConfig.hpp"' }
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
    filter "toolset:msc*"
        buildoptions { "/permissive-" }
    filter {}

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

        "src/Renderer/**.hpp",
        "src/Renderer/**.cpp",
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
        ["Renderer/*"] = { "src/Renderer/**.cpp", "src/Renderer/**.hpp" },
        ["ScientificRuntime/*"] = { "src/ScientificRuntime/**.cpp", "src/ScientificRuntime/**.hpp" },
        ["Storage/*"] = { "src/Storage/**.cpp", "src/Storage/**.hpp" },
    }

    includedirs {
        "tests",
        "src",
        "Vendor/spdlog/include",
        "Vendor/Tracy/public",
        "Vendor/GoogleTest/googletest/include",
        "Vendor/GoogleTest/googlemock/include",
        "Vendor/GLFW/include",
        "Vendor/GLAD/generated/include",
        "Vendor/ImGui",
        "Vendor/ImGui/backends",
        "Vendor/ImPlot",
        "Vendor/ImGuizmo",
        "Vendor/ImGuiColorTextEdit",
        "Vendor/imgui-command-palette",
        "Vendor/ImGuiNotify/win32Example/backends",
        "Vendor/stb",
        "Vendor/glm",
        "Vendor/entt/src",
        "Vendor/nativefiledialog-extended/src/include",
        "install/app/assets/fonts",
        "Vendor/thread-pool/include",
        "Vendor/json/include",
        "Vendor/yaml-cpp/include",
        "Vendor/stduuid/include"
    }

    defines { "DS_PYTHON_CAPI_AVAILABLE=0" }

    links {
        "GoogleTest",
        "GLFW",
        "GLAD",
        "ImGui",
        "ImPlot",
        "ImGuizmo",
        "ImGuiColorTextEdit",
        "nfd",
        "yaml-cpp"
    }

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
        defines { "DS_DEBUG", 'IMGUI_USER_CONFIG="Presentation/ImGuiUserConfig.hpp"' }
        symbols "On"

    filter "configurations:Release"
        defines { "DS_RELEASE" }
        optimize "On"

    filter "configurations:Dist"
        defines { "DS_DIST" }
        optimize "Full"

    filter {}
