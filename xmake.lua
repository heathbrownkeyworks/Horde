set_xmakever('3.0.1')
includes('lib/commonlibsse-ng')

set_project('Horde')
set_version('3.1.0')
set_license('GPL-3.0-or-later')

set_languages('c++23')
set_warnings('allextra')
set_policy('package.requires_lock', true)
add_requires('nlohmann_json')
set_toolset('msvc', 'ninja')

add_rules('mode.debug', 'mode.releasedbg', 'mode.release')

-- Horde's menu is a Magelight UI page (view/), staged by scripts/package.ps1.
-- Horde targets SE/AE; VR is not supported.

target('Horde')
    add_deps('commonlibsse-ng')
    add_packages('nlohmann_json')
    add_defines('NOMINMAX')

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

    -- Stage the complete DLL/web interface/ESP/license set explicitly with
    -- scripts/package.ps1. Building never replaces an active MO2 package.

target('HordeGameTextTests')
    set_kind('binary')
    set_default(false)
    set_rundir('$(projectdir)')
    add_packages('nlohmann_json')
    add_files('tests/GameTextTests.cpp')
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
