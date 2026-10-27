param([string]$PluginPath)

$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)

function Read-RepoFile {
    param([Parameter(Mandatory = $true)][string]$RelativePath)
    return Get-Content -Raw -LiteralPath (Join-Path $repoRoot $RelativePath)
}

function Assert-Contains {
    param(
        [Parameter(Mandatory = $true)][string]$Text,
        [Parameter(Mandatory = $true)][string]$Pattern,
        [Parameter(Mandatory = $true)][string]$Message
    )
    if ($Text -notmatch $Pattern) {
        throw $Message
    }
}

function Assert-NotContains {
    param(
        [Parameter(Mandatory = $true)][string]$Text,
        [Parameter(Mandatory = $true)][string]$Pattern,
        [Parameter(Mandatory = $true)][string]$Message
    )
    if ($Text -match $Pattern) {
        throw $Message
    }
}

$followerManager = Read-RepoFile 'src/follower/FollowerManager.cpp'
$packageManager = Read-RepoFile 'src/package/PackageManager.cpp'
$settings = Read-RepoFile 'src/Settings.cpp'

Assert-NotContains `
    $followerManager `
    'if\s*\(\s*actorBase\s*&&\s*!actorBase->IsEssential\(\)\s*\)\s*\{\s*actorBase->actorData\.actorBaseFlags\.set\(RE::ACTOR_BASE_DATA::Flag::kEssential\);' `
    'ScanForFollowers re-applies essential without checking FollowerData::isEssential.'

Assert-Contains `
    $followerManager `
    'if\s*\(\s*passive\s*\)\s*\{\s*if\s*\(\s*!data\.isPassive\s*\)' `
    'ApplyPassive must only capture original aggression when entering passive mode.'

Assert-NotContains `
    $followerManager `
    'void FollowerManager::SetWait[\s\S]*?PackageManager::GetSingleton\(\)\.ApplySandbox\(actor,\s*true\);' `
    'SetWait must not use the shared active sandbox marker for waiting followers.'

Assert-NotContains `
    $followerManager `
    'void FollowerManager::SetSandbox[\s\S]*?if\s*\(\s*data->isWaiting\s*\)[\s\S]*?pkgMgr\.ApplySandbox\(actor,\s*true\);' `
    'SetSandbox must not apply the shared active sandbox marker to waiting followers.'

Assert-NotContains `
    $followerManager `
    'void FollowerManager::UntrackFollower[\s\S]*?ApplyDismissedSandbox' `
    'Dismiss should not attach slot-scoped dismissed sandbox packages.'

Assert-Contains `
    $followerManager `
    'void FollowerManager::SetDismissedHome[\s\S]*?RefreshHomes\(\)' `
    'SetDismissedHome must apply the new home to the actor when loaded.'

Assert-Contains `
    $followerManager `
    'void FollowerManager::ClearHome[\s\S]*?RefreshHomes\(\)' `
    'ClearHome must restore the captured original editor location for active followers.'

Assert-Contains `
    $followerManager `
    'void FollowerManager::RefreshHomes[\s\S]*?ClearDismissedSandbox' `
    'ClearDismissedHome must clean stale dismissed sandbox aliases from older saves.'

Assert-NotContains `
    $packageManager `
    'ClearCompetingAliasPackages' `
    'The ClearCompetingAliasPackages wrapper must stay removed; ClearDialogueFollowerAlias is the only entry point.'

Assert-NotContains `
    $settings `
    'if\s*\(\s*_maxFollowers\s*<\s*kMaxFollowerCeiling\s*\)' `
    'Settings load must not reset every lower maxFollowers value to 20.'

# Hand-edited caps must remain within the available alias slots.
Assert-Contains `
    $settings `
    '_maxFollowers\s*=\s*std::clamp\(_maxFollowers,\s*1,\s*kMaxFollowerCeiling\)' `
    'Settings load must clamp maxFollowers to the alias-slot range.'

Assert-NotContains `
    $followerManager `
    'bool FollowerManager::TrackFollower' `
    'The unused TrackFollower path must stay removed; ScanForFollowers is the only recruitment route.'

# Forgetting a dismissed follower erases the only record of its original editor
# location, so the home has to be unwound first.
Assert-Contains `
    $followerManager `
    'void FollowerManager::ForgetFollower[\s\S]*?RestoreOriginalEditorLocation[\s\S]*?_registry\.erase\(it\)' `
    'ForgetFollower must restore the original editor location before erasing the registry entry.'

Assert-Contains `
    $settings `
    'RE::SendHUDMessage::ShowHUDMessage\(msg\)' `
    'Notifications must use the CommonLibSSE-NG 7 HUD message API.'

Assert-NotContains `
    $settings `
    'RE::DebugNotification' `
    'The removed CommonLibSSE-NG DebugNotification API must not be used.'

# Follower lifecycle and package ownership

$eventHandler = Read-RepoFile 'src/events/EventHandler.cpp'
$hordeUI = Read-RepoFile 'src/ui/HordeUI.cpp'
$questYaml = Read-RepoFile 'plugin/Horde/Quests/Horde_FollowerQuest - 000805_Horde.esp.yaml'

Assert-NotContains `
    $followerManager `
    'isSandboxEnabled\s*&&\s*PackageManager::GetSingleton\(\)\.HasSandbox' `
    'TeleportStrandedFollowers must not skip sandboxing followers - they are the ones most likely stranded.'

Assert-Contains `
    $eventHandler `
    'mgr\.ScanForFollowers\(\);[\s\S]{0,150}?mgr\.TeleportStrandedFollowers\(\)' `
    'Cell load must still scan and recover stranded followers with engine-driven sandboxing.'

Assert-NotContains ($packageManager + $eventHandler) `
    'UpdateIdleSandbox|RebuildHordeAliasInstance|Horde_IdleSandboxMarker|_sandboxActors|_idleTimer' `
    'Sandbox selection must use engine conditions, without marker movement, idle polling, or package-stack churn.'

Assert-Contains $packageManager `
    '"Horde_SandboxWaitPkg",\s*"Horde_SandboxActivePkg",\s*"Horde_WaitPkg",\s*"Horde_FollowPkg"' `
    'Runtime alias package priority must match the compiled ESP.'

Assert-Contains $packageManager `
    'previous->quest == quest && previous->alias == refAlias' `
    'Save-load refill must refresh only the matching Horde alias instance.'

Assert-Contains $packageManager `
    'anyEnabled = anyEnabled \|\| follower\.isSandboxEnabled' `
    'The sandbox global must include opted-in waiting followers.'

Assert-Contains $packageManager `
    'IsInFaction\(_sandboxFaction\) != follower\.isSandboxEnabled' `
    'Per-follower saved sandbox preferences must be synchronized independently.'

Assert-Contains $packageManager `
    'preferenceChanged \|\| globalChanged' `
    'Sandbox synchronization must only re-evaluate packages when settings change.'

Assert-Contains $followerManager `
    'void FollowerManager::SetSandbox[\s\S]*?data->isSandboxEnabled = enabled;\s*PackageManager::GetSingleton\(\)\.SyncSandboxSettings\(\)' `
    'Sandbox toggles must immediately synchronize the saved preference.'

Assert-Contains $followerManager `
    'void FollowerManager::UpdateDialogueGate\(\)\s*\{\s*PackageManager::GetSingleton\(\)\.SyncSandboxSettings\(\)' `
    'The lifecycle gate must restore sandbox state on track, scan, dismiss and post-load.'

Assert-Contains $followerManager `
    'void FollowerManager::OnPostLoadGame[\s\S]*?UpdateDialogueGate\(\)' `
    'Old-save recovery must synchronize the new global from persisted per-follower state.'

Assert-Contains $followerManager `
    'PackageManager::GetSingleton\(\)\.Reset\(\);\s*_followers\.clear\(\)' `
    'Revert must release sandbox state before dropping the previous save''s followers.'

Assert-Contains $followerManager `
    'void FollowerManager::Summon\([\s\S]*?SetPosition\(behind, true\);\s*//[^\r\n]*\s*actor->extraList\.RemoveByType\(RE::ExtraDataType::kPackageStartLocation\);\s*actor->EvaluatePackage' `
    'Summoning a waiting follower must reset its old package start location.'

Assert-Contains `
    $followerManager `
    'bool FollowerManager::UntrackFollower[\s\S]*?ClearSandboxState\(actor\)' `
    'Dismissal must remove the Horde-owned sandbox preference faction.'

Assert-Contains `
    $followerManager `
    'void FollowerManager::OnCosaveRevert[\s\S]*?ReleaseAllEssential\(\)' `
    'Cosave revert must unwind essential overrides so they do not leak across saves.'

Assert-NotContains `
    $eventHandler `
    'RestoreActorValue\(RE::ACTOR_VALUE_MODIFIER::kDamage, RE::ActorValue::kHealth, 999999\.0f\)' `
    'Friendly fire must never heal the target to full.'

Assert-NotContains $eventHandler 'RestoreActorValue|_healthBaseline|ConsumeHealthDeficit' `
    'Friendly-fire filtering must never refund sampled health deficits.'
Assert-Contains (Read-RepoFile 'src/events/HealthDamageHook.cpp') 'write_vfunc\(0x104, Thunk\)' `
    'Attributed damage must be filtered at Actor::HandleHealthDamage.'
Assert-Contains $eventHandler 'if \(block\) RequestTeamStandDown\(\)' `
    'Friendly hits must defer combat cleanup to the game thread.'
Assert-NotContains $hordeUI 'GetArmorRating\(\)\s*/' `
    'CommonLib armor ratings are already scaled.'
Assert-Contains $followerManager 'ReadCosavePayload\(\*a_intfc, length\)' `
    'Cosave allocation must follow bounded exact-length reads.'

Assert-NotContains `
    $eventHandler `
    'RestoreActorValue\(RE::ACTOR_VALUE_MODIFIER::kDamage' `
    'The removed three-argument RestoreActorValue overload must not be used.'

Assert-NotContains `
    $eventHandler `
    'SuppressFollowerBranches|RestoreFollowerBranches' `
    'Runtime dialogue-branch flag mutation must stay removed - INFO conditions handle suppression.'

Assert-NotContains `
    $hordeUI `
    'actor->GetEditorLocation1\(\)' `
    'Follower panel must report the live location, not the static editor location.'

Assert-Contains `
    $hordeUI `
    'crosshairPick->GetActiveTarget\(\)\.get\(\)' `
    'Crosshair targeting must use the CommonLibSSE-NG 7 flat/VR compatibility accessor.'

Assert-NotContains `
    $hordeUI `
    'crosshairPick->target\[' `
    'Horde must not index CrosshairPickData::target directly; flat builds expose a single handle.'

Assert-Contains `
    $hordeUI `
    'RunOnGameThread\(\[distancePreset\]\(\)[\s\S]{0,300}?ApplyFollowDistance\(distancePreset\)' `
    'ApplyFollowDistance must be dispatched to the game thread, not run on the UI callback thread.'

Assert-NotContains `
    $packageManager `
    'Horde_SandboxPkg|Horde_HomeSandbox|GetHomeMarker|ResetHomeMarker|ApplyDismissedSandbox' `
    'Dead sandbox package / home marker plumbing must stay removed.'

Assert-NotContains `
    $questYaml `
    '000804:Horde\.esp' `
    'Quest aliases must not reference the removed Horde_SandboxPkg.'

# --- Group passive (stand-down) toggle ---

$viewScript = Read-RepoFile 'view/horde.js'
$viewStyle = Read-RepoFile 'view/horde.css'

Assert-Contains `
    $followerManager `
    'void FollowerManager::ApplyPassive[\s\S]*?actor->StopCombat\(\)' `
    'Entering passive must break combat - aggression 0 alone does not end a fight in progress.'

Assert-Contains `
    $followerManager `
    'void FollowerManager::SetPassiveAll[\s\S]*?SetPassive\(id, passive, true\)' `
    'SetPassiveAll must drive per-follower SetPassive silently and emit one group notification.'

Assert-Contains `
    $eventHandler `
    'passiveStandDown = actorIsFollower && mgr\.IsPassive' `
    'Passive followers must be pulled out of any combat, not just intra-team combat.'

# Scope these checks to combat dispatch; the deferred worker must call StopCombat.
$combatHandler = [regex]::Match($eventHandler,
    '(?s)RE::BSEventNotifyControl EventHandler::ProcessEvent\(\s*const RE::TESCombatEvent\*.*?(?=\r?\nvoid EventHandler::RequestTeamStandDown\()').Value
if (-not $combatHandler) { throw 'Could not locate the TESCombatEvent handler.' }

Assert-Contains `
    $combatHandler `
    'passiveStandDown[\s\S]*?RequestTeamStandDown\(\)' `
    'Combat event must queue a deferred stand-down, not mutate combat state inline.'

Assert-NotContains `
    $combatHandler `
    'actor->StopCombat\(' `
    'Combat event handler must not call StopCombat() synchronously - it re-enters the same event.'

Assert-NotContains `
    $combatHandler `
    'actor->EvaluatePackage\(' `
    'Combat event handler must not call EvaluatePackage() synchronously inside combat dispatch.'

Assert-Contains `
    $eventHandler `
    'void EventHandler::RunTeamStandDown[\s\S]*?LookupByID<RE::Actor>' `
    'RunTeamStandDown must resolve actors from a snapshot on the game thread.'

Assert-Contains `
    $hordeUI `
    'hordePassiveAll' `
    'UI must expose the group passive listener.'

Assert-Contains `
    $viewScript `
    'order-passive''\)\.addEventListener\(''click''[\s\S]*?hordePassiveAll' `
    'Main window must carry the group passive toggle button.'

Assert-Contains `
    $viewScript `
    'model\.allPassive \? icon\(''swords''\) \+ ''All fight'' : icon\(''shield-off''\) \+ ''All passive''' `
    'Passive button must relabel to All fight while the group is standing down.'

# --- Horde: Passive power / keybind ---

$mainCpp = Read-RepoFile 'src/main.cpp'
$settingsHeader = Read-RepoFile 'src/Settings.h'
$passiveSpell = Join-Path $repoRoot 'plugin/Horde/Spells/Horde_PowerPassiveAll - 000837_Horde.esp.yaml'

# --- Horde 3.1.0 / Skyrim 1.7.104 compatibility ---

$xmake = Read-RepoFile 'xmake.lua'
$pluginHeader = Read-RepoFile 'src/plugin.h'
$commonLibCMake = Read-RepoFile 'lib/commonlibsse-ng/CMakeLists.txt'
$commonLibVersion = Read-RepoFile 'lib/commonlibsse-ng/include/SKSE/Version.h'
$commonLibInterfaces = Read-RepoFile 'lib/commonlibsse-ng/include/SKSE/Interfaces.h'
$runtimeCompatibilityPath = Join-Path $repoRoot 'src/RuntimeCompatibility.h'

Assert-Contains `
    $xmake `
    'set_version\([''"]3\.1\.0[''"]\)' `
    'The xmake project version must be Horde 3.1.0.'

Assert-Contains `
    $pluginHeader `
    'REL::Version\s+VERSION\s*\{\s*3\s*,\s*1\s*,\s*0\s*,\s*0\s*\}' `
    'The runtime log/version constant must be Horde 3.1.0.'

Assert-Contains `
    (Read-RepoFile 'README.md') `
    'Horde\s+3\.1\.0' `
    'The README must identify Horde 3.1.0.'

Assert-Contains `
    $commonLibCMake `
    'VERSION\s+7\.0\.0' `
    'Horde must build against the pinned CommonLibSSE-NG 7.0.0 snapshot.'

Assert-Contains `
    $commonLibVersion `
    'RUNTIME_SSE_1_7_99\s*\(\s*1\s*,\s*7\s*,\s*99\s*,\s*0\s*\)' `
    'CommonLibSSE-NG must contain the layout threshold used by Skyrim 1.7.104.'

Assert-Contains `
    $commonLibInterfaces `
    'kVersionIndependentEx_AddressLibraryV5' `
    'CommonLibSSE-NG must expose the Address Library v5 compatibility flag.'

if (-not (Test-Path -LiteralPath $runtimeCompatibilityPath)) {
    throw 'src/RuntimeCompatibility.h is missing.'
}

$runtimeCompatibility = Read-RepoFile 'src/RuntimeCompatibility.h'

Assert-Contains `
    $runtimeCompatibility `
    'version\.UsesAddressLibrary\(\)' `
    'Horde must advertise Address Library runtime independence.'

Assert-Contains `
    $runtimeCompatibility `
    'version\.UsesNoStructs\(\)' `
    'Horde must preserve structure-layout independence for its SE/AE build.'

Assert-Contains `
    $mainCpp `
    'SKSE_PLUGIN_VERSION\s*=\s*Plugin::RuntimeCompatibility::MakePluginVersionData\(\)' `
    'Horde must export the Address Library v5-capable PluginVersionData layout directly.'

Assert-Contains `
    $mainCpp `
    'kVersionIndependentEx_AddressLibraryV5' `
    'Horde must compile-time assert Address Library v5 compatibility.'

Assert-Contains `
    $mainCpp `
    'SKSEPlugin_Query' `
    'Horde must retain the legacy SKSEPlugin_Query export for Skyrim SE 1.5.97.'

# --- UI host / game-thread boundary ---
# Magelight UI hosts Horde's menu. Horde acquires it only through its public
# API header; without it the menu cannot open, but followers, powers and
# hotkeys work. Meridian UI is sunset and must not return as a host.
if (Test-Path -LiteralPath (Join-Path $repoRoot 'src/MeridianUIAPI')) { throw 'Meridian UI is sunset; src/MeridianUIAPI must not return.' }
foreach ($source in @(Get-ChildItem -LiteralPath (Join-Path $repoRoot 'src') -Recurse -File -Include *.cpp, *.h) +
                    @(Get-ChildItem -LiteralPath (Join-Path $repoRoot 'view') -Recurse -File -Include *.js, *.css, *.html)) {
    if ((Get-Content -Raw -LiteralPath $source.FullName) -match 'Meridian') { throw "$($source.Name) references Meridian; Magelight UI is Horde's only host." }
}
Assert-Contains $hordeUI 'if \(!InitializeMagelight\(\)\)\s*logger::warn\("HordeUI: no UI host; the menu needs Magelight UI 1\.1 or newer"\)' 'Without Magelight UI, Horde must log what the menu needs.'
Assert-Contains $hordeUI 'void HordeUI::Toggle\(\)[\s\S]*?if \(!HasHost\(\)\)[\s\S]*?ShowHUDMessage\("Horde''s menu needs Magelight UI 1\.1 or newer\."\)' 'Opening the menu without Magelight UI must tell the player what is missing.'
Assert-NotContains $hordeUI 'GetProcAddress' 'Horde must acquire Magelight UI only through the RequestApi4 loader in its API header.'
Assert-Contains $hordeUI 'L"Data" / L"Magelight" / L"Horde" / L"index\.html"' 'HordeUI must load the page from Data/Magelight/Horde/.'
Assert-Contains (Read-RepoFile 'scripts/package.ps1') 'Join-Path \$destination ''Magelight/Horde''' 'The package must install the page where HordeUI loads it.'
Assert-Contains $hordeUI 'RequestUIMode\(_view, kUIModeFlagPause\)' 'The menu must pause the game while open.'
Assert-Contains $hordeUI 'SetEscapeCapture\(view, true\)' 'On Magelight the page must own Escape so it can close its own dialogs first.'
Assert-Contains $hordeUI 'case Event::UIModeExited:[\s\S]{0,80}case Event::UIModeRefused:[\s\S]{0,300}ShowView\(ui\._view, false\)' 'Every way Magelight ends UI mode must close Horde''s menu.'
# Magelight calls listeners on its render thread inside the frame, where SKSE's
# AddTask can hang; page commands must reach the game thread through PostGameTask.
$magelightListener = [regex]::Match($hordeUI, '(?s)void HordeUI::OnMagelightListener\(.*?(?=\r?\nvoid HordeUI::)').Value
if (-not $magelightListener) { throw 'Could not locate HordeUI::OnMagelightListener.' }
Assert-Contains $magelightListener 'PostGameTask' 'Magelight page commands must be posted with PostGameTask.'
Assert-NotContains $magelightListener 'AddTask' 'Magelight page commands must not use SKSE''s AddTask on the render thread.'
Assert-Contains $viewScript 'const host = window\.magelight;[\s\S]{0,120}host\.send\(name, JSON\.stringify\(payload \|\| \{\}\)\)' 'The page must reach HordeUI''s listeners through magelight.send as JSON text.'
Assert-Contains $hordeUI 'Magelight could not create the Horde view' 'A failed view creation must be logged.'
Assert-Contains $mainCpp 'void OnDataLoaded\(\)[\s\S]*?FollowerManager::GetSingleton\(\)\.Initialize\(\)[\s\S]*?HordeUI::GetSingleton\(\)\.Initialize\(\)[\s\S]*?KeyHandler::RegisterSink\(\)' 'The menu, follower systems and hotkeys must initialize together.'
Assert-Contains $mainCpp 'kPreLoadGame:[\s\S]{0,120}HordeUI::GetSingleton\(\)\.Close\(\)' 'Save loading must close the menu.'
Assert-Contains (Read-RepoFile 'src/keyhandler/keyhandler.cpp') 'if \(HordeUI::GetSingleton\(\)\.IsOpen\(\)\)' 'Group hotkeys must stand down while the page owns the keyboard.'

# --- No native interface ---
if (Test-Path -LiteralPath (Join-Path $repoRoot 'src/ui/imgui')) { throw 'The native ImGui interface was removed; src/ui/imgui must not return.' }
if (Test-Path -LiteralPath (Join-Path $repoRoot 'assets/fonts')) { throw 'The native interface fonts were removed; assets/fonts must not return.' }
Assert-NotContains $xmake 'imgui|''stb''|d3dcompiler' 'Horde must not link Dear ImGui, stb or the D3D shader compiler.'
foreach ($source in Get-ChildItem -LiteralPath (Join-Path $repoRoot 'src') -Recurse -File -Include *.cpp, *.h) {
    if ((Get-Content -Raw -LiteralPath $source.FullName) -match 'imgui|ImGui') { throw "$($source.Name) references ImGui; Horde's only interface is its Magelight UI page." }
}

# --- Interface scale ---
# The page fits a 1600x900 stage to the screen, then applies the player's scale.
Assert-Contains $settingsHeader 'kMinUIScale = 70;[\s\S]*?kMaxUIScale = 170;' 'Interface scale must range from 70% to 170%.'
Assert-Contains $settingsHeader 'int\s+_uiScale = 100;' 'Interface scale must default to 100%.'
Assert-Contains $settings 'std::clamp\(j\.value\("uiScale", _uiScale\), kMinUIScale, kMaxUIScale\)' 'A hand-edited uiScale must be clamped on load.'
Assert-Contains $hordeUI 'j\["uiScale"\] = settings\.GetUIScale\(\)' 'State pushes must carry the interface scale.'
Assert-Contains $hordeUI 'j\.contains\("uiScale"\)[\s\S]{0,80}SetUIScale' 'The settings listener must accept uiScale.'
Assert-Contains $viewScript 'SCALE_MIN = 70, SCALE_MAX = 170' 'The slider range must match the native clamp.'
Assert-Contains $viewScript 'Math\.min\(w / STAGE_W, h / STAGE_H\), 0\.5, 3\) \* ui\.scale' 'The fitted stage zoom must apply the interface scale.'
Assert-Contains $viewScript '''<div class="settings-grid">'' \+\s*''<section class="panel setting setting-wide"><div><h3>Interface scale</h3>' 'Interface scale must be the first setting.'
# At 170% a 16:9 stage is about 941x529 logical pixels; the layout must condense.
foreach ($step in 'compact', 'narrow', 'short', 'shorter') {
    Assert-Contains $viewScript "classList\.toggle\('$step', (width|height) < \d+\)" "fit() must mark the $step stage size."
}
Assert-Contains $viewStyle '\.app\.narrow \.dossier-body \{ grid-template-columns: minmax\(0, 1fr\); \}' 'A narrow stage must stack the follower details.'
Assert-Contains $viewStyle '\.app\.narrow \.capacity-count em, \.app\.narrow \.pips \{ display: none; \}' 'A narrow title bar must keep the close button on screen.'

# --- Game text encoding and language ---
# Plugin text may be Windows-1252/1251 rather than UTF-8; the strict JSON
# serializer throws type_error.316 on it, on the game thread.
$gameText = Read-RepoFile 'src/GameText.h'
Assert-Contains $gameText 'error_handler_t::replace' 'Game text must serialize with U+FFFD replacement, not the strict error handler.'
Assert-NotContains $hordeUI '\.dump\(' 'HordeUI must serialize game text through Horde::DumpGameJSON.'
Assert-NotContains $followerManager '\.dump\(' 'Co-save records must serialize follower names through Horde::DumpGameJSON.'
Assert-Contains $hordeUI 'void HordeUI::PushStateToView\(\)[\s\S]*?try[\s\S]*?catch \(const std::exception' 'State pushes run on the game thread and must not throw.'
Assert-Contains $hordeUI 'GetSetting\("sLanguage:General"\)' 'The page must learn the game language from Skyrim''s INI setting.'
Assert-Contains $hordeUI 'state\["language"\]\s*=\s*_language' 'State pushes must carry the page language.'
Assert-Contains $viewScript 'state\.language[\s\S]{0,120}setLanguage\(state\.language\)' 'The page must apply the language native sends.'
Assert-Contains $viewScript 'document\.documentElement\.lang\s*=\s*tag' 'The page must set its document language.'
Assert-Contains $viewScript 'new Intl\.Collator\(tag' 'The page must sort names in the game language.'
Assert-Contains $viewStyle "--ef-font-display:\s*'Saira Condensed',\s*'Fira Sans Extra Condensed'" 'Cyrillic and Greek display text must use the condensed fallback before system fonts.'
foreach ($subset in 'cyrillic', 'cyrillic-ext', 'greek') {
    foreach ($weight in 400, 500, 600, 700) {
        Assert-Contains $viewStyle "fonts/fira-sans-extra-condensed-$subset-$weight-normal\.woff" "Display weight $weight lacks the $subset subset."
    }
}
foreach ($font in [regex]::Matches($viewStyle, 'url\((fonts/[^)]+)\)') | ForEach-Object { $_.Groups[1].Value }) {
    if ($font -notmatch '\.woff$') { throw "view/horde.css loads $font; Ultralight decodes WOFF, not WOFF2." }
    if (-not (Test-Path -LiteralPath (Join-Path $repoRoot "view/$font"))) { throw "view/horde.css loads missing view/$font." }
}
if (-not (Test-Path -LiteralPath (Join-Path $repoRoot 'licenses/FiraSansExtraCondensed-OFL.txt'))) {
    throw 'The Fira Sans Extra Condensed license is missing from licenses/.'
}
Assert-Contains (Read-RepoFile 'THIRD_PARTY_NOTICES.md') 'licenses/FiraSansExtraCondensed-OFL\.txt' 'Third-party notices must list Fira Sans Extra Condensed.'

if (-not (Test-Path -LiteralPath $passiveSpell)) {
    throw 'Horde_PowerPassiveAll spell record is missing from the plugin source.'
}

Assert-Contains `
    $mainCpp `
    '"Horde_PowerPassiveAll"' `
    'Passive power must be in kHordePowerEditorIDs so it is granted, favorited, and removed with the others.'

Assert-Contains `
    $eventHandler `
    'powerPassive && castID == powerPassive->GetFormID\(\)[\s\S]{0,300}?SetPassiveAll\(!mgr\.IsAllPassive\(\)\)' `
    'Casting the Passive power must toggle group passive, matching the UI button.'

Assert-Contains `
    $mainCpp `
    'passiveKey[\s\S]{0,400}?SetPassiveAll\(!mgr\.IsAllPassive\(\)\)' `
    'Passive keybind must toggle group passive so keybind-mode users can reach the feature.'

Assert-Contains `
    $settingsHeader `
    '_passiveAllKey' `
    'Passive keybind must be configurable in settings.'

# Explicit numeric interrupt flags preserve the full values through Spriggit.
# Verify the compiled artifact as well as the source and lifecycle contracts.
$espPath = if ($PluginPath) { $PluginPath } else { Join-Path $repoRoot 'plugin/Horde.esp' }
if (Test-Path -LiteralPath $espPath) {
    & node (Join-Path $PSScriptRoot 'horde_recruitment_inventory.mjs') $espPath
    if ($LASTEXITCODE -ne 0) { throw 'Horde recruitment inventory checks failed.' }

    & node (Join-Path $PSScriptRoot 'horde_plugin_improvements.mjs') $espPath
    if ($LASTEXITCODE -ne 0) { throw 'Horde plugin improvements checks failed.' }

    & node (Join-Path $PSScriptRoot 'horde_release_fixes.mjs') $espPath
    if ($LASTEXITCODE -ne 0) { throw 'Horde release repair checks failed.' }

    $espBytes = [System.IO.File]::ReadAllBytes($espPath)
    $needle = [System.Text.Encoding]::ASCII.GetBytes('PKDT')
    $found = 0
    $unpatched = 0
    for ($i = 0; $i -lt ($espBytes.Length - 20); $i++) {
        if ($espBytes[$i] -eq $needle[0] -and $espBytes[$i+1] -eq $needle[1] -and
            $espBytes[$i+2] -eq $needle[2] -and $espBytes[$i+3] -eq $needle[3]) {
            $found++
            $flags = [System.BitConverter]::ToUInt16($espBytes, $i + 6 + 8)
            if (($flags -band 0xFE00) -ne 0xFE00) { $unpatched++ }
        }
    }
    if ($found -eq 0) { throw 'No PKDT subrecords found in Horde.esp - package data is missing.' }
    if ($unpatched -gt 0) {
        throw "Horde.esp has $unpatched package(s) missing InterruptFlags bits 9-15. Restore the explicit numeric flags in package YAML and rebuild with Spriggit."
    }
    Write-Host "  Verified InterruptFlags on $found package(s) in Horde.esp."
}

Write-Host 'Horde static regression checks passed.'
