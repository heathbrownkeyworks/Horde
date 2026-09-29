set_xmakever('3.0.1')
includes('lib/commonlibsse-ng')

set_project('Horde')
set_version('3.0.0')
set_license('GPL-3.0-or-later')

set_languages('c++23')
set_warnings('allextra')
set_policy('package.requires_lock', true)
add_requires('nlohmann_json')
add_requires('imgui v1.92.6', {configs = {dx11 = true, win32 = true, freetype = true}})
add_requires('stb')
set_toolset('msvc', 'ninja')

add_rules('mode.debug', 'mode.releasedbg', 'mode.release')

-- The native ImGui host targets SE/AE. VR needs a separate compositor/input backend.

target('Horde')
    add_deps('commonlibsse-ng')
    add_packages('nlohmann_json', 'imgui')
    add_defines('NOMINMAX')
    add_syslinks('d3d11', 'dxgi', 'd3dcompiler')

    -- Use CommonLib's resource/packaging rule without its generated
    -- SKSEPluginInfo declaration. Horde exports PluginVersionData directly so
    -- SKSE 2.3.1 can see the Address Library v5 compatibility bit.
    add_rules('commonlib.plugin')
    on_load(function(target)
        target:data_set('commonlib.plugin.config', {
            name        = 'Horde',
            author      = 'ColdSun',
            description = 'An SKSE Follower System for Skyrim SE/AE.'
        })
    end)

    add_files('src/**.cpp')
    add_headerfiles('src/**.h')

    add_includedirs(
        'src',
        '$(projectdir)'
    )

    set_pcxxheader('src/pch.h')

    -- Stage the complete DLL/fonts/ESP/license set explicitly with
    -- scripts/package.ps1. Building never replaces an active MO2 package.

target('HordeImGuiScreenTests')
    set_kind('binary')
    set_default(false)
    set_rundir('$(projectdir)')
    add_packages('nlohmann_json', 'imgui')
    add_files('tests/ImGuiScreenTests.cpp', 'src/ui/imgui/HordeScreen.cpp', 'src/ui/imgui/Theme.cpp')
    add_includedirs('src')
    add_defines('NOMINMAX', 'WIN32_LEAN_AND_MEAN')

target('HordeImGuiPreview')
    set_kind('binary')
    set_default(false)
    set_rundir('$(projectdir)')
    add_packages('nlohmann_json', 'imgui', 'stb')
    add_files('tools/imgui-preview/main.cpp', 'src/ui/imgui/HordeScreen.cpp', 'src/ui/imgui/Theme.cpp')
    add_includedirs('src')
    add_defines('NOMINMAX', 'WIN32_LEAN_AND_MEAN')
    add_syslinks('d3d11', 'dxgi', 'd3dcompiler', 'user32', 'gdi32', 'shell32')

target('HordeImGuiInputTests')
    set_kind('binary')
    set_default(false)
    set_rundir('$(projectdir)')
    add_files('tests/ImGuiInputTests.cpp')
    add_includedirs('src')

target('HordeFollowerRuntimeTests')
    set_kind('binary')
    set_default(false)
    set_rundir('$(projectdir)')
    add_packages('nlohmann_json')
    add_files('tests/FollowerRuntimeTests.cpp')
    add_includedirs('src', 'build/horde-qa')
    before_build(function ()
        os.execv('node', {'scripts/qa/generate_runtime_tests.mjs'})
    end)
