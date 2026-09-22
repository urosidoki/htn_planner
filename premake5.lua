-- HTN
newoption {
    trigger = "sdk",
    description = "Generate HTNSDK: distributable framework, runtime bridge and translator only"
}
local sdk = _OPTIONS["sdk"] ~= nil
local function ProjectLocation(inName)
    return sdk and ("build/sdk/" .. inName) or inName
end
newoption {
    trigger = "no-benchmark-allocations",
    description = "Compile HTNBenchmark without replacement new/delete allocation probes"
}
newoption {
    trigger = "generated-execution-profiling",
    description = "Enable coarse generated cold/warm execution diagnostics"
}

newoption {
    trigger = "atom-diagnostics",
    description = "Enable HTNAtom owned-resource diagnostics in all generated projects (Profile; detailed in ProfileDetailed)"
}

newoption {
    trigger = "runtime-backtracking-support",
    value = "MODE",
    description = "Generate support for changing HTN backtracking mode at runtime",
    default = "disabled",
    allowed = {
        { "disabled", "Disable runtime-selectable backtracking support" },
        { "enabled", "Enable runtime-selectable backtracking support" }
    }
}

workspace(sdk and "HTNSDK" or "HTN")
    location "."
    startproject(sdk and "HTNTranslator" or "HTNEditor")
    architecture "x64"
    if sdk then
        configurations {
            "StaticDebugPlain", "StaticDebugInstrumented",
            "StaticReleasePlain", "StaticReleaseInstrumented",
            "DynamicDebugPlain", "DynamicDebugInstrumented",
            "DynamicReleasePlain", "DynamicReleaseInstrumented"
        }
    else
        configurations { "Debug", "Profile", "ProfileDetailed", "Release" }
    end

    warnings "Extra"
    flags { "FatalWarnings" }

    if _OPTIONS["generated-execution-profiling"] then
        defines { "HTN_GENERATED_EXECUTION_PROFILING" }
    end

    -- HTN_VALIDATE_DOMAIN enables validating the domain during a decomposition
    -- HTN_DEBUG_DECOMPOSITION enables storing the state of each step of a decomposition for debug purposes

    filter "configurations:Debug"
        defines { "HTN_DEBUG", "HTN_ENABLE_LOGGING", "HTN_VALIDATE_DOMAIN", "HTN_DEBUG_DECOMPOSITION" }
        symbols "On"

    -- Representative profiling build. All fine-grained generated/runtime instrumentation
    -- is compiled out; the Domain Runner only measures whole decomposition wall time.
    filter "configurations:Profile"
        defines { "HTN_PROFILE" }
        if _OPTIONS["atom-diagnostics"] then
            defines { "HTN_MEMORY_ATOM_DIAGNOSTICS" }
        end
        runtime "Release"
        optimize "Full"
        symbols "On"

    -- Diagnostic profiling build. Same optimizer settings as Profile, but compile in
    -- the exhaustive hierarchical generated/runtime profiler used to locate hotspots.
    filter "configurations:ProfileDetailed"
        defines { "HTN_PROFILE", "HTN_PROFILE_DETAILED" }
        if _OPTIONS["atom-diagnostics"] then
            -- Explicit in generated projects too; HTNAtomC.h also keeps DETAILED -> base for consumers.
            defines { "HTN_MEMORY_ATOM_DIAGNOSTICS", "HTN_MEMORY_ATOM_DIAGNOSTICS_DETAILED" }
        end
        runtime "Release"
        optimize "Full"
        symbols "On"

    -- Shipping/runtime build. Domain structure and symbol validity are resolved by the
    -- loader/semantic/compiler pipeline. Logging, runtime validation and decomposition
    -- diagnostics remain Debug-only; Profile configurations compile them out as well.
    filter "configurations:Release"
        defines { "HTN_RELEASE" }
        runtime "Release"
        optimize "Full"

    if sdk then
        filter {}
        symbols "On"
        for _, linkage in ipairs { "Static", "Dynamic" } do
            for _, crt in ipairs { "Debug", "Release" } do
                for _, instrumentation in ipairs { "Plain", "Instrumented" } do
                    filter ("configurations:" .. linkage .. crt .. instrumentation)
                        staticruntime(linkage == "Static" and "On" or "Off")
                        runtime(crt)
                        optimize(crt == "Debug" and "Off" or "Full")
                        defines { crt == "Debug" and "_DEBUG" or "NDEBUG" }
                        defines { crt == "Debug" and "HTN_DEBUG" or "HTN_RELEASE" }
                        if instrumentation == "Instrumented" then
                            defines { "HTN_ENABLE_LOGGING", "HTN_VALIDATE_DOMAIN", "HTN_DEBUG_DECOMPOSITION" }
                        end
                end
            end
        end
    end
    filter "system:windows"
        systemversion "latest"

outputdir = (sdk and "sdk/" or "") .. "%{cfg.buildcfg}-%{cfg.system}-%{cfg.architecture}"

-- HTN generated-domain build helpers. Keep these outside individual projects so
-- HTNDemo and HTNTest use exactly the same generation pipeline.
local function MakeHTNEntryPoint(inDomainPath)
    local domainName = path.getbasename(inDomainPath)
    local result = ""
    local makeUpper = true

    for i = 1, #domainName do
        local c = domainName:sub(i, i)
        if c == "_" or c == "-" or c == " " then
            makeUpper = true
        else
            if makeUpper then
                result = result .. c:upper()
                makeUpper = false
            else
                result = result .. c
            end
        end
    end

    return "Create" .. result .. "HTN"
end

local function GetHTNRootDomainFiles()
    local result = {}
    local domainFiles = os.matchfiles("Domains/**.domain")
    table.sort(domainFiles)

    for _, domainFile in ipairs(domainFiles) do
        local normalizedDomainFile = path.translate(domainFile, "/")

        -- Domains/Includes contains link-time modules, not independently executable planners.
        if not normalizedDomainFile:find("^Domains/Includes/") then
            table.insert(result, normalizedDomainFile)
        end
    end

    return result
end

local function MakeHTNDomainGenerationCommands(inGeneratedDirectory)
    local commands = {}
    local failFast = os.host() == "windows" and " || exit /b 1" or " || exit 1"

    for _, normalizedDomainFile in ipairs(GetHTNRootDomainFiles()) do
        local entryPoint = MakeHTNEntryPoint(normalizedDomainFile)

        table.insert(
            commands,
            '"%{wks.location}/bin/' .. outputdir .. '/HTNTranslator/HTNTranslator.exe" ' ..
            '"%{wks.location}/' .. normalizedDomainFile .. '" ' ..
            entryPoint ..
            ' "%{wks.location}/' .. inGeneratedDirectory .. '"' ..
            (_OPTIONS["runtime-backtracking-support"] == "enabled" and ' --runtime-backtracking-support=enabled' or '') .. failFast)
    end

    return commands
end

local function MakeHTNGeneratedSourceFiles(inGeneratedDirectory)
    local generatedFiles = {}

    for _, domainFile in ipairs(GetHTNRootDomainFiles()) do
        table.insert(
            generatedFiles,
            inGeneratedDirectory .. "/" .. path.getbasename(domainFile) .. ".generated.c")
    end

    return generatedFiles
end

-- HTNFramework
group "Runtime"
project "HTNFramework"
    location(ProjectLocation("HTNFramework"))
    kind "StaticLib"
    language "C++"
    cppdialect "C++20"

    targetdir ("bin/" .. outputdir .. "/%{prj.name}")
    objdir ("int/" .. outputdir .. "/%{prj.name}")

    pchheader "pch.h"
    pchsource "%{prj.name}/src/pch.cpp"
    forceincludes "pch.h"

    files { "%{prj.name}/src/**.cpp",
            "%{prj.name}/src/**.h",
            "%{prj.name}/src/**.inl",
            "%{prj.name}/**.natvis",
            -- Optick
            "ThirdParty/optick/src/**.cpp",
            "ThirdParty/optick/src/**.h" }

    if sdk then
        removefiles {
            "%{prj.name}/src/Domain/Tooling/**.cpp"
        }
    end

    includedirs { "%{prj.name}/src", "HTNFramework/src", "ThirdParty/optick/src" }

-- Optional reference integration. The Framework never depends on this library.
project "HTNIntegration"
    location(ProjectLocation("HTNIntegration"))
    kind "StaticLib"
    language "C++"
    cppdialect "C++20"
    targetdir ("bin/" .. outputdir .. "/%{prj.name}")
    objdir ("int/" .. outputdir .. "/%{prj.name}")
    files { "%{prj.name}/src/**.cpp", "%{prj.name}/src/**.h" }
    includedirs { "HTNIntegration/src", "HTNFramework/src", "ThirdParty/optick/src" }
    forceincludes "pch.h"
    links { "HTNFramework" }

-- Shared bridge used by generated-domain dynamic modules. The host supplies
-- the actual runtime functions once, so modules never duplicate framework state.
project "HTNRuntimeBridge"
    location(ProjectLocation("HTNRuntimeBridge"))
    kind "SharedLib"
    language "C++"
    cppdialect "C++20"

    targetdir ("bin/" .. outputdir .. (sdk and "/HTNRuntimeBridge" or "/HTNTest"))
    objdir ("int/" .. outputdir .. "/%{prj.name}")
    files { "%{prj.name}/src/**.cpp", "HTNFramework/src/Translator/HTNRuntimeBridge.h" }
    includedirs { "HTNFramework/src" }
    defines { "HTN_RUNTIME_BRIDGE_EXPORTS" }

if not sdk then
group "Tests/Fixtures"
local function AddGeneratedModuleProject(inName, inEntryPoint, inOutputDirectory, inABIVersion)
    project(inName)
        location(inName)
        kind "SharedLib"
        language "C++"
        cppdialect "C++20"

        targetdir ("bin/" .. outputdir .. "/HTNTest")
        objdir ("int/" .. outputdir .. "/%{prj.name}")
        files { inOutputDirectory .. "/backtracking_policy.generated.c" }
        includedirs { "HTNFramework/src" }
        defines { "HTN_GENERATED_MODULE_EXPORTS" }
        if inABIVersion then
            defines { "HTN_GENERATED_PLANNER_ABI_VERSION=" .. inABIVersion }
        end
        links { "HTNRuntimeBridge" }
        dependson { "HTNTranslator" }
        prebuildcommands {
            '"%{wks.location}/bin/' .. outputdir .. '/HTNTranslator/HTNTranslator.exe" ' ..
            '"%{wks.location}/Domains/Test/backtracking_policy.domain" ' .. inEntryPoint ..
            ' "%{wks.location}/' .. inOutputDirectory .. '"' ..
            (os.host() == "windows" and " || exit /b 1" or " || exit 1")
        }
end

AddGeneratedModuleProject("HTNTestDomainModule", "CreateBacktrackingPolicyModuleHTN", "HTNTestDomainModule/generated")
AddGeneratedModuleProject("HTNTestIncompatibleDomainModule", "CreateBacktrackingPolicyIncompatibleHTN", "HTNTestIncompatibleDomainModule/generated", "0x48540001u")

-- HTNDemo
group "Demos"
project "HTNDemo"
    location "HTNDemo"
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++20"

    targetdir ("bin/" .. outputdir .. "/%{prj.name}")
    objdir ("int/" .. outputdir .. "/%{prj.name}")

    files { "%{prj.name}/src/**.cpp",
            "%{prj.name}/src/**.h",
            MakeHTNGeneratedSourceFiles("%{prj.name}/generated"),
            "ThirdParty/optick/src/**.cpp",
            "ThirdParty/optick/src/**.h",
            "ThirdParty/imgui/imconfig.h",
            "ThirdParty/imgui/imgui.cpp",
            "ThirdParty/imgui/imgui.h",
            "ThirdParty/imgui/imgui_demo.cpp",
            "ThirdParty/imgui/imgui_draw.cpp",
            "ThirdParty/imgui/imgui_internal.h",
            "ThirdParty/imgui/imgui_tables.cpp",
            "ThirdParty/imgui/imgui_widgets.cpp",
            "ThirdParty/imgui/backends/imgui_impl_sdl2.h",
            "ThirdParty/imgui/backends/imgui_impl_sdl2.cpp",
            "ThirdParty/imgui/backends/imgui_impl_sdlrenderer2.h",
            "ThirdParty/imgui/backends/imgui_impl_sdlrenderer2.cpp" }

    includedirs { "HTNIntegration/src", "%{prj.name}/src", "HTNFramework/src", "ThirdParty/optick/src", "ThirdParty/SDL2/include", "ThirdParty/imgui", "ThirdParty/imgui/backends" }

    libdirs { "ThirdParty/SDL2/lib/%{cfg.architecture}" }
    links { "HTNIntegration", "HTNFramework", "SDL2", "SDL2main" }
    dependson { "HTNTranslator" }

    -- Every root domain is a separate compilation unit. There is deliberately no
    -- generated unity source or global registry: hosts keep per-domain granularity.
    -- Adding/removing a root domain requires regenerating project files so the
    -- corresponding *.generated.c is added/removed from this target.
    prebuildcommands(MakeHTNDomainGenerationCommands("HTNDemo/generated"))

    postbuildcommands { "{COPYFILE} ../ThirdParty/SDL2/lib/%{cfg.architecture}/SDL2.dll %{cfg.targetdir}" }

-- Isolated reload playground. Only this project opts the reused NPC code into
-- explicit release/reattach; HTNDemo keeps its normal generated/static path.
group "Demos/HotReload"
project "HTNHotReloadDemoDomain"
    location "HTNHotReloadDemoDomain"
    kind "SharedLib"
    language "C++"
    cppdialect "C++20"
    targetname "WandererHTN"
    targetdir ("bin/" .. outputdir .. "/HTNHotReloadDemo")
    objdir ("int/" .. outputdir .. "/%{prj.name}")
    files { "HTNHotReloadDemo/generated/Wanderer.generated.c" }
    includedirs { "HTNFramework/src" }
    defines { "HTN_GENERATED_MODULE_EXPORTS" }
    links { "HTNRuntimeBridge" }
    dependson { "HTNTranslator" }
    prebuildcommands {
        '"%{wks.location}/bin/' .. outputdir .. '/HTNTranslator/HTNTranslator.exe" ' ..
        '"%{wks.location}/Domains/Wanderer.domain" CreateWandererHotReloadHTN ' ..
        '"%{wks.location}/HTNHotReloadDemo/generated"' ..
        (os.host() == "windows" and " || exit /b 1" or " || exit 1")
    }

project "HTNHotReloadDemo"
    location "HTNHotReloadDemo"
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++20"
    targetdir ("bin/" .. outputdir .. "/%{prj.name}")
    objdir ("int/" .. outputdir .. "/%{prj.name}")
    debugdir "%{wks.location}"
    defines { "HTN_HOT_RELOAD_DEMO", 'HTN_HOT_RELOAD_CONFIGURATION="%{cfg.buildcfg}"' }
    files { "HTNHotReloadDemo/src/**.cpp", "HTNHotReloadDemo/src/**.h", "HTNHotReloadDemo/CompileDomain.cmd",
            "HTNDemo/src/AI/AIHTNDemoWandererAgent.cpp", "HTNDemo/src/AI/AIHTNDemoWanderer.cpp",
            "HTNDemo/src/AI/AIHTNDemoPathfinder.cpp", "HTNDemo/src/AI/AIHTNDemoGridTerrainDaemon.cpp",
            "HTNDemo/src/World/**.cpp", "HTNDemo/src/UI/HTNNPCSimulationPanel.cpp",
            "HTNDemo/src/UI/HTNGeneratedImGuiHelpers.cpp",
            "ThirdParty/imgui/imgui.cpp", "ThirdParty/imgui/imgui_draw.cpp",
            "ThirdParty/imgui/imgui_tables.cpp", "ThirdParty/imgui/imgui_widgets.cpp",
            "ThirdParty/imgui/misc/cpp/imgui_stdlib.cpp",
            "ThirdParty/imgui/backends/imgui_impl_sdl2.cpp", "ThirdParty/imgui/backends/imgui_impl_sdlrenderer2.cpp" }
    includedirs { "HTNIntegration/src", "HTNHotReloadDemo/src", "HTNDemo/src", "HTNFramework/src",
                  "ThirdParty/optick/src", "ThirdParty/imgui", "ThirdParty/SDL2/include" }
    libdirs { "ThirdParty/SDL2/lib/%{cfg.architecture}" }
    links { "HTNIntegration", "HTNFramework", "SDL2", "SDL2main" }
    dependson { "HTNHotReloadDemoDomain", "HTNRuntimeBridge", "HTNTranslator" }
    postbuildcommands {
        "{COPYFILE} %{wks.location}/ThirdParty/SDL2/lib/%{cfg.architecture}/SDL2.dll %{cfg.targetdir}",
        "{COPYFILE} %{wks.location}/bin/" .. outputdir .. "/HTNTest/HTNRuntimeBridge.dll %{cfg.targetdir}",
        "{COPYFILE} %{wks.location}/bin/" .. outputdir .. "/HTNTest/HTNRuntimeBridge.lib %{cfg.targetdir}"
    }

-- HTNEditor
group "Tools"
project "HTNEditor"
    location "HTNEditor"
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++20"

    targetdir ("bin/" .. outputdir .. "/%{prj.name}")
    objdir ("int/" .. outputdir .. "/%{prj.name}")

    files { "%{prj.name}/src/**.cpp",
            "%{prj.name}/src/**.h",
            "ThirdParty/imgui/imconfig.h",
            "ThirdParty/imgui/imgui.cpp",
            "ThirdParty/imgui/imgui.h",
            "ThirdParty/imgui/imgui_draw.cpp",
            "ThirdParty/imgui/imgui_tables.cpp",
            "ThirdParty/imgui/imgui_widgets.cpp",
            "ThirdParty/imgui/misc/cpp/imgui_stdlib.cpp",
            "ThirdParty/imgui/misc/cpp/imgui_stdlib.h",
            "ThirdParty/imgui/backends/imgui_impl_sdl2.h",
            "ThirdParty/imgui/backends/imgui_impl_sdl2.cpp",
            "ThirdParty/imgui/backends/imgui_impl_sdlrenderer2.h",
            "ThirdParty/imgui/backends/imgui_impl_sdlrenderer2.cpp" }

    includedirs { "%{prj.name}/src", "HTNFramework/src", "ThirdParty/optick/src",
                  "ThirdParty/SDL2/include", "ThirdParty/imgui", "ThirdParty/imgui/backends" }

    libdirs { "ThirdParty/SDL2/lib/%{cfg.architecture}" }
    links { "HTNFramework", "SDL2", "SDL2main" }
    filter "system:windows"
        links { "Comdlg32" }
    filter {}

    postbuildcommands { "{COPYFILE} ../ThirdParty/SDL2/lib/%{cfg.architecture}/SDL2.dll %{cfg.targetdir}" }


-- HTNLanguageServer
project "HTNLanguageServer"
    location "HTNLanguageServer"
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++20"

    targetdir ("bin/" .. outputdir .. "/%{prj.name}")
    objdir ("int/" .. outputdir .. "/%{prj.name}")

    files { "%{prj.name}/src/**.cpp",
            "%{prj.name}/src/**.h" }

    includedirs { "%{prj.name}/src", "HTNFramework/src", "ThirdParty/optick/src" }

    links { "HTNFramework" }

end -- Full solution's fixtures, debugger, demos and authoring tools.

-- HTNTranslator
group "Tools"
project "HTNTranslator"
    location(ProjectLocation("HTNTranslator"))
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++20"

    targetdir ("bin/" .. outputdir .. "/%{prj.name}")
    objdir ("int/" .. outputdir .. "/%{prj.name}")

    files { "%{prj.name}/src/**.cpp",
            "%{prj.name}/src/**.h" }

    includedirs { "%{prj.name}/src", "HTNFramework/src", "ThirdParty/optick/src" }

    links { "HTNFramework" }

-- HTNBenchmark
if not sdk then
group "Benchmarks"
project "HTNBenchmark"
    location "HTNBenchmark"
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++20"

    if not _OPTIONS["no-benchmark-allocations"] then
        defines { "HTN_BENCHMARK_ALLOCATIONS" }
    end

    targetdir ("bin/" .. outputdir .. "/%{prj.name}")
    objdir ("int/" .. outputdir .. "/%{prj.name}")

    files { "%{prj.name}/src/**.cpp",
            "%{prj.name}/src/**.h",
            MakeHTNGeneratedSourceFiles("%{prj.name}/generated") }

    includedirs { "HTNIntegration/src", "%{prj.name}/src", "HTNFramework/src" }

    links { "HTNIntegration", "HTNFramework" }
    dependson { "HTNTranslator" }
    prebuildcommands(MakeHTNDomainGenerationCommands("HTNBenchmark/generated"))

-- HTNTest
group "Tests"
    project "HTNTest"
    location "HTNTest"
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++20"

    targetdir ("bin/" .. outputdir .. "/%{prj.name}")
    objdir ("int/" .. outputdir .. "/%{prj.name}")

    files { "%{prj.name}/src/**.cpp",
            "%{prj.name}/src/**.h",
            MakeHTNGeneratedSourceFiles("%{prj.name}/generated"),
            "%{prj.name}/generated/backtracking_policy_overflow/backtracking_policy.generated.c",
            "%{prj.name}/generated/backtracking_policy_fixed_small/backtracking_policy.generated.c",
            "%{prj.name}/generated/backtracking_policy_fixed_enough/backtracking_policy.generated.c",
            -- Optick
            "ThirdParty/optick/src/**.cpp",
            "ThirdParty/optick/src/**.h" }

    includedirs { "HTNIntegration/src", "%{prj.name}/src", "HTNFramework/src", "ThirdParty/optick/src", "ThirdParty/imgui" }

    links { "HTNIntegration", "HTNFramework" }
    dependson { "HTNTranslator", "HTNTestDomainModule", "HTNTestIncompatibleDomainModule" }
    filter "system:not windows"
        links { "dl" }
    filter {}
    prebuildcommands(MakeHTNDomainGenerationCommands("HTNTest/generated"))
    prebuildcommands {
        '"%{wks.location}/bin/' .. outputdir .. '/HTNTranslator/HTNTranslator.exe" "%{wks.location}/Domains/Test/backtracking_policy.domain" CreateBacktrackingPolicyOverflowHTN "%{wks.location}/HTNTest/generated/backtracking_policy_overflow" --backtracking-policy=fixed-with-overflow --backtracking-capacity=2' .. (_OPTIONS["runtime-backtracking-support"] == "enabled" and " --runtime-backtracking-support=enabled" or "") .. (os.host() == "windows" and " || exit /b 1" or " || exit 1"),
        '"%{wks.location}/bin/' .. outputdir .. '/HTNTranslator/HTNTranslator.exe" "%{wks.location}/Domains/Test/backtracking_policy.domain" CreateBacktrackingPolicyFixedSmallHTN "%{wks.location}/HTNTest/generated/backtracking_policy_fixed_small" --backtracking-policy=fixed-capacity --backtracking-capacity=2' .. (_OPTIONS["runtime-backtracking-support"] == "enabled" and " --runtime-backtracking-support=enabled" or "") .. (os.host() == "windows" and " || exit /b 1" or " || exit 1"),
        '"%{wks.location}/bin/' .. outputdir .. '/HTNTranslator/HTNTranslator.exe" "%{wks.location}/Domains/Test/backtracking_policy.domain" CreateBacktrackingPolicyFixedEnoughHTN "%{wks.location}/HTNTest/generated/backtracking_policy_fixed_enough" --backtracking-policy=fixed-capacity --backtracking-capacity=3' .. (_OPTIONS["runtime-backtracking-support"] == "enabled" and " --runtime-backtracking-support=enabled" or "") .. (os.host() == "windows" and " || exit /b 1" or " || exit 1")
    }

    nuget { "Microsoft.googletest.v140.windesktop.msvcstl.static.rt-dyn:1.8.1.7" }
end -- Full solution's benchmarks and tests.
group ""
