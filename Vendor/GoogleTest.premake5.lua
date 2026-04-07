project "GoogleTest"
    location (_DS_ROOT .. "/build/generated/%{_ACTION}")
    kind "StaticLib"
    language "C++"
    cppdialect "C++latest"
    staticruntime "off"
    warnings "Off"

    targetdir (_DS_ROOT .. "/build/bin/%{cfg.buildcfg}-%{cfg.system}-%{cfg.architecture}/%{prj.name}")
    objdir (_DS_ROOT .. "/build/bin-int/%{cfg.buildcfg}-%{cfg.system}-%{cfg.architecture}/%{prj.name}")

    files {
        _DS_ROOT .. "/Vendor/GoogleTest/googletest/src/gtest-all.cc"
    }

    includedirs {
        _DS_ROOT .. "/Vendor/GoogleTest/googletest",
        _DS_ROOT .. "/Vendor/GoogleTest/googletest/include"
    }

    filter "system:windows"
        systemversion "latest"

    filter "system:linux"
        pic "On"

    filter "configurations:Debug"
        symbols "On"

    filter "configurations:Release"
        optimize "On"

    filter "configurations:Dist"
        optimize "Full"

    filter {}

project "GoogleTestMain"
    location (_DS_ROOT .. "/build/generated/%{_ACTION}")
    kind "StaticLib"
    language "C++"
    cppdialect "C++latest"
    staticruntime "off"
    warnings "Off"

    targetdir (_DS_ROOT .. "/build/bin/%{cfg.buildcfg}-%{cfg.system}-%{cfg.architecture}/%{prj.name}")
    objdir (_DS_ROOT .. "/build/bin-int/%{cfg.buildcfg}-%{cfg.system}-%{cfg.architecture}/%{prj.name}")

    files {
        _DS_ROOT .. "/Vendor/GoogleTest/googletest/src/gtest_main.cc"
    }

    includedirs {
        _DS_ROOT .. "/Vendor/GoogleTest/googletest",
        _DS_ROOT .. "/Vendor/GoogleTest/googletest/include"
    }

    links { "GoogleTest" }

    filter "system:windows"
        systemversion "latest"

    filter "system:linux"
        pic "On"

    filter "configurations:Debug"
        symbols "On"

    filter "configurations:Release"
        optimize "On"

    filter "configurations:Dist"
        optimize "Full"

    filter {}
