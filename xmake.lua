set_xmakever('3.0.1')
includes('lib/commonlibsse-ng')

set_project('Horde')
set_version('2.0.0')
set_license('MIT')

set_languages('c++23')
set_warnings('allextra')
set_policy('package.requires_lock', true)
add_requires('nlohmann_json')
set_toolset('msvc', 'ninja')

add_rules('mode.debug', 'mode.releasedbg', 'mode.release')

-- CommonLib still produces a multi-runtime native DLL, but Horde's Meridian
-- UI dependency currently makes the complete mod SE/AE-only. VR is deferred
-- until Meridian has a dedicated VR compositor/input backend.

target('Horde')
    add_deps('commonlibsse-ng')
    add_packages('nlohmann_json')

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

    after_install(function(target)
        local modsPath = os.getenv("XSE_TES5_MODS_PATH")
        if modsPath then
            local viewSrc = path.join(os.projectdir(), "view")
            local viewDst = path.join(modsPath, "Horde", "MeridianUI", "horde")
            os.mkdir(viewDst)
            os.cp(path.join(viewSrc, "*"), viewDst)
            print("copied views to " .. viewDst)

            local espSrc = path.join(os.projectdir(), "plugin", "Horde.esp")
            local espDst = path.join(modsPath, "Horde", "Horde.esp")
            if os.isfile(espSrc) then
                os.cp(espSrc, espDst)
                print("copied ESP to " .. espDst)
            else
                print("skipped ESP copy; generate plugin/Horde.esp from the Spriggit source first")
            end
        end
    end)
