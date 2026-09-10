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
    'void FollowerManager::SetDismissedHome[\s\S]*?ApplyHomeEditorLocation' `
    'SetDismissedHome must apply the new home to the actor when loaded.'

Assert-Contains `
    $followerManager `
    'void FollowerManager::ClearHome[\s\S]*?RestoreOriginalEditorLocation' `
    'ClearHome must restore the captured original editor location for active followers.'

Assert-Contains `
    $followerManager `
    'void FollowerManager::ClearDismissedHome[\s\S]*?ClearDismissedSandbox' `
    'ClearDismissedHome must clean stale dismissed sandbox aliases from older saves.'

Assert-Contains `
    $packageManager `
    'void PackageManager::ClearCompetingAliasPackages\(RE::Actor\* actor\)\s*\{\s*ClearDialogueFollowerAlias\(actor\);' `
    'ClearCompetingAliasPackages must be scoped to vanilla DialogueFollower cleanup.'

Assert-NotContains `
    $settings `
    'if\s*\(\s*_maxFollowers\s*<\s*kMaxFollowerCeiling\s*\)' `
    'Settings load must not reset every lower maxFollowers value to 20.'

Assert-Contains `
    $settings `
    'RE::SendHUDMessage::ShowHUDMessage\(msg\)' `
    'Notifications must use the CommonLibSSE-NG 7 HUD message API.'

Assert-NotContains `
    $settings `
    'RE::DebugNotification' `
    'The removed CommonLibSSE-NG DebugNotification API must not be used.'

# --- 1.8.1 hardening pass ---

$eventHandler = Read-RepoFile 'src/events/EventHandler.cpp'
$hordeUI = Read-RepoFile 'src/ui/HordeUI.cpp'
$questYaml = Read-RepoFile 'plugin/Horde/Quests/Horde_FollowerQuest - 000805_Horde.esp.yaml'

Assert-NotContains `
    $followerManager `
    'isSandboxEnabled\s*&&\s*PackageManager::GetSingleton\(\)\.HasSandbox' `
    'TeleportStrandedFollowers must not skip sandboxing followers - they are the ones most likely stranded.'

Assert-Contains `
    $eventHandler `
    'SuspendSandbox\(\)[\s\S]*?TeleportStrandedFollowers\(\)' `
    'Cell load must suspend sandbox before running stranded-follower recovery.'

Assert-Contains `
    $packageManager `
    'bool inCombat = player->IsInCombat\(\);' `
    'UpdateIdleSandbox must cancel sandbox during combat.'

Assert-NotContains `
    $packageManager `
    '_idleTimer \+= 5\.0f;' `
    'Idle timer must accumulate real elapsed seconds, not a hardcoded poll interval.'

Assert-Contains `
    $followerManager `
    'bool FollowerManager::UntrackFollower[\s\S]*?ForgetSandboxActor\(formID\)' `
    'UntrackFollower must drop sandbox bookkeeping even when the actor fails to resolve.'

Assert-Contains `
    $followerManager `
    'void FollowerManager::OnCosaveRevert[\s\S]*?ReleaseAllEssential\(\)' `
    'Cosave revert must unwind essential overrides so they do not leak across saves.'

Assert-NotContains `
    $eventHandler `
    'RestoreActorValue\(RE::ACTOR_VALUE_MODIFIER::kDamage, RE::ActorValue::kHealth, 999999\.0f\)' `
    'Friendly fire must restore only the damage dealt, not heal the target to full.'

Assert-Contains `
    $eventHandler `
    'RestoreActorValue\(RE::ActorValue::kHealth, deficit\)' `
    'Friendly-fire recovery must use the CommonLibSSE-NG 7 actor-value API.'

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
    'AddTask\(\[distancePreset\]\(\)[\s\S]{0,300}?ApplyFollowDistance\(distancePreset\)' `
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

$indexHtml = Read-RepoFile 'view/index.html'

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

# Re-entrancy guard. Calling StopCombat()/EvaluatePackage() inside TESCombatEvent
# dispatch makes the attacker's combat controller re-acquire the follower and
# re-fire the same event on the same stack, recursing until the stack overflows
# (which SEH crash loggers cannot capture, so it crashes with no crash log).
Assert-Contains `
    $eventHandler `
    'passiveStandDown[\s\S]{0,1200}?RequestTeamStandDown\(\)' `
    'Combat event must queue a deferred stand-down, not mutate combat state inline.'

Assert-NotContains `
    $eventHandler `
    'passiveStandDown\s*=\s*actorIsFollower[\s\S]{0,700}?actor->StopCombat\(\)' `
    'Combat event handler must not call StopCombat() synchronously - it re-enters the same event.'

Assert-NotContains `
    $eventHandler `
    'passiveStandDown\s*=\s*actorIsFollower[\s\S]{0,700}?actor->EvaluatePackage\(' `
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
    $indexHtml `
    'id="btnPassiveAll"' `
    'Main window must carry the Passive/Aggressive toggle button.'

Assert-Contains `
    $indexHtml `
    "allPassive \? 'Aggressive' : 'Passive'" `
    'Passive button must relabel to Aggressive while the group is standing down.'

# --- Horde: Passive power / keybind ---

$mainCpp = Read-RepoFile 'src/main.cpp'
$settingsHeader = Read-RepoFile 'src/Settings.h'
$passiveSpell = Join-Path $repoRoot 'plugin/Horde/Spells/Horde_PowerPassiveAll - 000837_Horde.esp.yaml'

# --- Horde 2.1.0 / Skyrim 1.7.104 compatibility ---

$xmake = Read-RepoFile 'xmake.lua'
$pluginHeader = Read-RepoFile 'src/plugin.h'
$commonLibCMake = Read-RepoFile 'lib/commonlibsse-ng/CMakeLists.txt'
$commonLibVersion = Read-RepoFile 'lib/commonlibsse-ng/include/SKSE/Version.h'
$commonLibInterfaces = Read-RepoFile 'lib/commonlibsse-ng/include/SKSE/Interfaces.h'
$runtimeCompatibilityPath = Join-Path $repoRoot 'src/RuntimeCompatibility.h'

Assert-Contains `
    $xmake `
    'set_version\([''"]2\.1\.0[''"]\)' `
    'The xmake project version must be Horde 2.1.0.'

Assert-Contains `
    $pluginHeader `
    'REL::Version\s+VERSION\s*\{\s*2\s*,\s*1\s*,\s*0\s*,\s*0\s*\}' `
    'The runtime log/version constant must be Horde 2.1.0.'

Assert-Contains `
    (Read-RepoFile 'view/index.html') `
    '<span class="panel-version">v2\.1\.0</span>' `
    'The UI must display Horde v2.1.0.'

Assert-Contains `
    (Read-RepoFile 'README.md') `
    'Horde 2\.1\.0 is a lightweight' `
    'The README must identify Horde 2.1.0.'

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

# --- Meridian.View startup lifecycle ---

Assert-Contains `
    $mainCpp `
    'void OnInputLoaded\(\)[\s\S]*?Meridian::UI::View::Query' `
    'Meridian.View/1 must be acquired during kInputLoaded on Meridian''s CEF application thread.'

Assert-Contains `
    $mainCpp `
    'case\s+SKSE::MessagingInterface::kInputLoaded:\s*OnInputLoaded\(\);' `
    'The SKSE message handler must route kInputLoaded to Meridian acquisition.'

Assert-NotContains `
    $mainCpp `
    'void OnDataLoaded\(\)[\s\S]*?Meridian::UI::View::Query' `
    'OnDataLoaded must not perform first Meridian/CEF initialization from its worker thread.'

Assert-NotContains `
    $mainCpp `
    'void OnDataLoaded\(\)[\s\S]*?if\s*\(\s*!g_MeridianView\s*\)\s*\{[\s\S]{0,300}?return\s*;' `
    'A missing Meridian UI must not return before Horde core systems and input paths initialize.'

Assert-Contains `
    $mainCpp `
    'void OnDataLoaded\(\)[\s\S]*?FollowerManager::GetSingleton\(\)\.Initialize\(\)[\s\S]*?EventHandler::Register\(\)[\s\S]*?KeyHandler::RegisterSink\(\)' `
    'OnDataLoaded must always initialize Horde core systems and hotkeys independently of the optional UI.'

Assert-Contains `
    $mainCpp `
    'if\s*\(\s*g_MeridianView\s*\)\s*\{\s*HordeUI::GetSingleton\(\)\.Initialize\(\);' `
    'HordeUI initialization must be gated narrowly on a successfully acquired Meridian interface.'

# --- Optional Meridian.Input controller integration ---
$controllerJs = Read-RepoFile 'view/controller.js'
Assert-Contains $mainCpp 'OnInputLoaded\(\)[\s\S]*?Meridian::UI::Input::Query' `
    'The optional Input/1 interface must be acquired alongside View/1 during kInputLoaded.'
Assert-Contains $hordeUI 'if\s*\(\s*!g_MeridianInput\s*\)\s*return;' `
    'Missing Input/1 must not prevent keyboard/mouse UI operation.'
Assert-Contains $hordeUI 'config\.enabled\s*=\s*1;' 'Horde must opt its view into controller input.'
Assert-Contains $hordeUI 'Result::Conflict' 'A controller opener conflict must have an explicit fallback.'
Assert-Contains $hordeUI 'hordeHidePanel[\s\S]*?Unfocus\(_view\)[\s\S]*?Hide\(_view\)' `
    'Native close must end page scopes before releasing the view.'
Assert-Contains $controllerJs 'input\.attachNavigation' 'Horde must use Meridian navigation scopes.'
Assert-Contains $controllerJs 'input\.getPrompt' 'Controller prompts must use the current bindings.'
Assert-NotContains $controllerJs 'navigator\.getGamepads|XInputGetState|SendInput\(' `
    'Horde must not add independent controller polling.'
Assert-Contains $indexHtml '<script src="controller\.js"></script>' 'The controller consumer asset must be loaded.'

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

# The ESP is rebuilt from YAML by Spriggit, which drops PACK InterruptFlags bits
# 9-15 every time. Verify the shipped binary still carries them.
$espPath = Join-Path $repoRoot 'plugin/Horde.esp'
if (Test-Path -LiteralPath $espPath) {
    & node (Join-Path $PSScriptRoot 'horde_recruitment_inventory.mjs') $espPath
    if ($LASTEXITCODE -ne 0) { throw 'Horde recruitment inventory checks failed.' }

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
        throw "Horde.esp has $unpatched package(s) missing InterruptFlags bits 9-15. Run: node plugin/patch_interrupt_flags.mjs"
    }
    Write-Host "  Verified InterruptFlags on $found package(s) in Horde.esp."
}

Write-Host 'Horde static regression checks passed.'
