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

$nativeScreen = Read-RepoFile 'src/ui/imgui/HordeScreen.cpp'

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
    $nativeScreen `
    '"passive-all"' `
    'Main window must carry the Passive/Aggressive toggle button.'

Assert-Contains `
    $nativeScreen `
    'passive\s*\?\s*"Aggressive"\s*:\s*"Passive"' `
    'Passive button must relabel to Aggressive while the group is standing down.'

# --- Horde: Passive power / keybind ---

$mainCpp = Read-RepoFile 'src/main.cpp'
$settingsHeader = Read-RepoFile 'src/Settings.h'
$passiveSpell = Join-Path $repoRoot 'plugin/Horde/Spells/Horde_PowerPassiveAll - 000837_Horde.esp.yaml'

# --- Horde 3.0.1 / Skyrim 1.7.104 compatibility ---

$xmake = Read-RepoFile 'xmake.lua'
$pluginHeader = Read-RepoFile 'src/plugin.h'
$commonLibCMake = Read-RepoFile 'lib/commonlibsse-ng/CMakeLists.txt'
$commonLibVersion = Read-RepoFile 'lib/commonlibsse-ng/include/SKSE/Version.h'
$commonLibInterfaces = Read-RepoFile 'lib/commonlibsse-ng/include/SKSE/Interfaces.h'
$runtimeCompatibilityPath = Join-Path $repoRoot 'src/RuntimeCompatibility.h'

Assert-Contains `
    $xmake `
    'set_version\([''"]3\.0\.1[''"]\)' `
    'The xmake project version must be Horde 3.0.1.'

Assert-Contains `
    $pluginHeader `
    'REL::Version\s+VERSION\s*\{\s*3\s*,\s*0\s*,\s*1\s*,\s*0\s*\}' `
    'The runtime log/version constant must be Horde 3.0.1.'

Assert-Contains `
    (Read-RepoFile 'src/ui/imgui/HordeScreen.cpp') `
    '"v3\.0\.1"' `
    'The UI must display Horde v3.0.1.'

Assert-Contains `
    (Read-RepoFile 'README.md') `
    'Horde\s+3\.0\.1' `
    'The README must identify Horde 3.0.1.'

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

# --- Native ImGui startup / game-thread boundary ---
$nativeHost = Read-RepoFile 'src/ui/imgui/ImGuiHost.cpp'
$nativeMenu = Read-RepoFile 'src/ui/imgui/HordeMenu.cpp'
Assert-NotContains $mainCpp 'ViewDllLoader|InputDllLoader|GetProcAddress' 'The native runtime must not acquire an external UI API.'
Assert-NotContains $hordeUI 'ExecuteJavaScript|RegisterListener\(|CreateView\(' 'The native facade must not depend on a browser.'
Assert-Contains $mainCpp 'void OnDataLoaded\(\)[\s\S]*?FollowerManager::GetSingleton\(\)\.Initialize\(\)[\s\S]*?HordeUI::GetSingleton\(\)\.Initialize\(\)[\s\S]*?KeyHandler::RegisterSink\(\)' 'Native UI, follower systems and hotkeys must initialize together.'
Assert-Contains $hordeUI 'void HordeUI::Dispatch[\s\S]*?AddTask[\s\S]*?host\.Generation\(\)\s*!=\s*generation' 'Native commands must be game-thread tasks guarded by their opening generation.'
Assert-Contains $nativeMenu 'F::kPausesGame' 'Horde must retain its paused menu behavior.'
Assert-Contains $nativeHost 'ImGui_ImplDX11_RenderDrawData' 'The native host must render through DX11.'
Assert-NotContains $nativeHost 'ImGui_ImplWin32_NewFrame|XInputGetState|SendInput\(' 'Runtime controller input must use Skyrim events without independent polling.'
Assert-NotContains $nativeHost 'HordeUI::GetSingleton\(\)\.Toggle\(' 'Controller input must not open Horde; use the lesser power.'
Assert-Contains $nativeHost 'if \(s\.held\.load\(\) == 0\)\s*s\.armed = true' 'Held controller buttons must be released before menu navigation begins.'
Assert-Contains $mainCpp 'kPreLoadGame:[\s\S]{0,120}HordeUI::GetSingleton\(\)\.Close\(\)' 'Save loading must close the native menu.'
Assert-Contains $nativeHost 'ContextScope' 'Rendering must restore another native mod''s ImGui context.'

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
