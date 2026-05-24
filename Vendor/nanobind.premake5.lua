local function applyNanobindConfigurationFilters()
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

local nanobindEmbedAvailable = _DS_PYTHON_EMBED_AVAILABLE == true

project "nanobind"
    location (_DS_ROOT .. "/build/generated/%{_ACTION}")
    language "C++"
    cppdialect "C++17"
    staticruntime "off"
    warnings "Extra"
    flags { "MultiProcessorCompile" }

    targetdir (_DS_ROOT .. "/build/bin/" .. outputdir .. "/%{prj.name}")
    objdir (_DS_ROOT .. "/build/bin-int/" .. outputdir .. "/%{prj.name}")

    if nanobindEmbedAvailable then
        kind "StaticLib"
        files {
            _DS_ROOT .. "/Vendor/nanobind/src/nb_combined.cpp"
        }

        includedirs {
            _DS_ROOT .. "/Vendor/nanobind/include",
            _DS_ROOT .. "/Vendor/nanobind/ext/robin_map/include",
            _DS_PYTHON_INCLUDE_DIR
        }

        defines {
            "NB_STATIC"
        }

        filter { "action:vs2022" }
            buildoptions { "/utf-8", "/bigobj" }

        filter "system:linux"
            pic "On"
            buildoptions { "-fno-strict-aliasing", "-fvisibility=hidden" }

        filter {}
    else
        kind "Utility"
    end

    applyNanobindConfigurationFilters()

project "DefectStudioPythonBridge"
    location (_DS_ROOT .. "/build/generated/%{_ACTION}")
    language "C++"
    cppdialect "C++latest"
    staticruntime "off"
    warnings "Extra"
    flags { "MultiProcessorCompile" }

    targetdir (_DS_ROOT .. "/build/bin/" .. outputdir .. "/%{prj.name}")
    objdir (_DS_ROOT .. "/build/bin-int/" .. outputdir .. "/%{prj.name}")

    if nanobindEmbedAvailable then
        kind "SharedLib"
        targetname "ds_python_bridge"
        targetprefix ""

        files {
            _DS_ROOT .. "/src/ScientificRuntime/PythonBindings/**.hpp",
            _DS_ROOT .. "/src/ScientificRuntime/PythonBindings/**.cpp"
        }

        includedirs {
            _DS_ROOT .. "/src",
            _DS_ROOT .. "/Vendor/nanobind/include",
            _DS_ROOT .. "/Vendor/nanobind/ext/robin_map/include",
            _DS_PYTHON_INCLUDE_DIR
        }

        links {
            "nanobind",
            _DS_PYTHON_LIB_NAME
        }

        libdirs {
            _DS_PYTHON_LIB_DIR
        }

        defines {
            "NB_STATIC"
        }

        filter "system:windows"
            targetextension ".pyd"
            defines { "NOMINMAX" }

        filter { "system:windows", "action:vs2022" }
            systemversion "latest"
            buildoptions { "/utf-8" }

        filter "system:linux"
            targetextension ".so"
            pic "On"

        filter {}
    else
        kind "Utility"
    end

    applyNanobindConfigurationFilters()
