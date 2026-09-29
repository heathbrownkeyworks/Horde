// Compile production method bodies against a small deterministic engine fixture.
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
const root = fileURLToPath(new URL('../../', import.meta.url));
const out = path.join(root, 'build/horde-qa');
fs.mkdirSync(out, {recursive:true});
const read = name => fs.readFileSync(path.join(root, name), 'utf8').replaceAll('\r\n', '\n');
const groups = {
    'src/follower/FollowerManager.cpp': [
        'RE::TESFaction* FollowerManager::GetCurrentFollowerFaction(',
        'RE::TESFaction* FollowerManager::GetHordeFollowerFaction(',
        'RE::Actor* FollowerManager::ResolveActor(', 'FollowerData* FollowerManager::FindFollower(',
        'void FollowerManager::ApplyProtection(', 'void FollowerManager::ReleaseEssential(',
        'int FollowerManager::CaptureOriginalProtection(', 'void FollowerManager::ReleaseAllEssential(',
        'bool FollowerManager::IsTracked(', 'void FollowerManager::OnCosaveLoad(', 'void FollowerManager::OnCosaveSave(',
        'void FollowerManager::OnCosaveRevert(', 'bool FollowerManager::UntrackFollower(',
        'void FollowerManager::SoftUntrack(', 'void FollowerManager::ScanForFollowers(',
        'void FollowerManager::OnPostLoadGame(', 'void FollowerManager::UpsertRegistry(',
        'void FollowerManager::SyncRegistryHome(', 'void FollowerManager::RestoreHome(',
        'void FollowerManager::CaptureOriginalEditorLoc(', 'bool FollowerManager::ApplyHomeEditorLocation(',
        'bool FollowerManager::RestoreOriginalEditorLocation(', 'void FollowerManager::RefreshHomes(',
        'void FollowerManager::SetHome(', 'void FollowerManager::ClearHome(',
        'void FollowerManager::SetDismissedHome(', 'void FollowerManager::ClearDismissedHome(',
        'void FollowerManager::ForgetFollower('
    ],
    'src/events/EventHandler.cpp': [
        'bool EventHandler::ShouldBlockTeamDamage(', 'void EventHandler::RunTeamStandDown(',
        'void EventHandler::RequestFastScan(', 'void EventHandler::SuspendSession(',
        'void EventHandler::ResumeSession('
    ],
    'src/package/PackageManager.cpp': [
        'bool PackageManager::HasAnimalAlias(', 'void PackageManager::ReleaseAnimal(',
        'bool PackageManager::EnsureResidencePackage(', 'bool PackageManager::ClearResidencePackage('
    ]
};
let output = '// Generated from production sources. Do not edit.\n';
for (const [file, signatures] of Object.entries(groups)) {
    const source = read(file);
    for (const signature of signatures) {
        const start = source.indexOf(signature);
        if (start < 0) throw Error(`Missing ${signature}`);
        const end = source.indexOf('\n}', start);
        if (end < start) throw Error(`Unterminated ${signature}`);
        const line = source.slice(0,start).split('\n').length;
        output += `\n#line ${line} "${path.join(root,file).replaceAll('\\','/')}"\n${source.slice(start,end+2)}\n`;
    }
}
const write = (file, text) => {
    if (!fs.existsSync(file) || fs.readFileSync(file,'utf8') !== text) fs.writeFileSync(file,text);
};
write(path.join(out,'RuntimeMethods.generated.h'),output);
write(path.join(out,'FollowerData.generated.h'),read('src/follower/FollowerData.h').replace('#include "pch.h"',''));
console.log('Generated runtime fixtures from production methods and JSON serializers.');
