project "Tracy"
    location (_DS_ROOT .. "/build/generated/%{_ACTION}")
    kind "StaticLib"
    language "C++"
    cppdialect "C++latest"
    staticruntime "off"
    warnings "Off"

    targetdir (_DS_ROOT .. "/build/bin/%{cfg.buildcfg}-%{cfg.system}-%{cfg.architecture}/%{prj.name}")
    objdir (_DS_ROOT .. "/build/bin-int/%{cfg.buildcfg}-%{cfg.system}-%{cfg.architecture}/%{prj.name}")

    files {
        _DS_ROOT .. "/Vendor/Tracy/public/TracyClient.cpp"
    }

    includedirs {
        _DS_ROOT .. "/Vendor/Tracy/public"
    }

    defines { "TRACY_ENABLE" }

    filter "system:windows"
        systemversion "latest"
        defines { "TRACY_NO_SYSTEM_TRACING" }

    filter "system:linux"
        pic "On"

    filter "configurations:Debug"
        symbols "On"

    filter "configurations:Release"
        optimize "On"

    filter "configurations:Dist"
        optimize "Full"

    filter {}
