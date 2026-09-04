#!/usr/bin/env node
// Generates INFO override YAMLs for Horde.esp that suppress vanilla follower
// dismiss/wait/follow dialogue for Horde-tracked followers.
//
// Each override copies the source INFO and appends:
//   GetInFaction Horde_FollowerFaction < 1
// Horde followers are at rank 1, so this condition fails -> option hidden.
// Non-Horde actors are not in the faction -> condition passes -> option shows.
//
// IMPORTANT — pick the right source.
// These are full-record overrides. Whatever is in the source dump is what Horde
// ships, so generating from raw Skyrim.esm makes Horde revert any other mod's
// edits to these topics (USSEP fixes, Relationship Dialogue Overhaul, etc.) when
// Horde wins the conflict. Generate from a Spriggit dump of the LOAD ORDER
// WINNER for these three topics, not from vanilla, whenever another mod in the
// target load order touches them.
//
// Usage:
//   node generate_dialogue_overrides.mjs [sourceDialogTopicsDir] [outputDialogTopicsDir]
//
// The source is required so the caller deliberately chooses the winning
// records. Output defaults to the checked-in Horde DialogTopics directory.

import { readFileSync, writeFileSync, mkdirSync, readdirSync } from 'fs';
import { join } from 'path';
import { fileURLToPath } from 'url';

const SOURCE_BASE = process.argv[2];
const OUTPUT_BASE = process.argv[3] ?? fileURLToPath(new URL('./Horde/DialogTopics/', import.meta.url));

if (!SOURCE_BASE) {
    console.error('Usage: node generate_dialogue_overrides.mjs <sourceDialogTopicsDir> [outputDialogTopicsDir]');
    process.exit(1);
}

console.log(`source: ${SOURCE_BASE}`);
console.log(`output: ${OUTPUT_BASE}\n`);

const TOPICS = [
    'DialogueFollowerDismissTopic - 05C80C_Skyrim.esm',
    'DialogueFollowerWaitTopic - 075084_Skyrim.esm',
    'DialogueFollowerFollowTopic - 075083_Skyrim.esm',
];

const NEW_CONDITION = `- MutagenObjectType: ConditionFloat
  CompareOperator: LessThan
  Data:
    MutagenObjectType: GetInFactionConditionData
    Faction: 00082F:Horde.esp
  ComparisonValue: 1`;

function addHordeCondition(content) {
    content = content.replace(/\r\n/g, '\n').replace(/\r/g, '\n');
    const lines = content.split('\n');
    let condStart = -1;
    let condEnd = -1;

    for (let i = 0; i < lines.length; i++) {
        const line = lines[i];
        if (line === 'Conditions:') {
            condStart = i;
        } else if (condStart >= 0 && condEnd < 0) {
            // Inside conditions block — end when we hit another top-level key
            if (line.length > 0 && !line.startsWith(' ') && !line.startsWith('-') && !line.startsWith('\r')) {
                condEnd = i;
                break;
            }
        }
    }

    if (condStart < 0) {
        // No existing conditions block — append one
        return content.trimEnd() + '\nConditions:\n' + NEW_CONDITION + '\n';
    }

    if (condEnd < 0) {
        // Conditions is the last block in the file
        return content.trimEnd() + '\n' + NEW_CONDITION + '\n';
    }

    // Insert new condition just before the next top-level key
    lines.splice(condEnd, 0, NEW_CONDITION);
    return lines.join('\n');
}

let total = 0;
for (const topic of TOPICS) {
    const vanillaDir = join(SOURCE_BASE, topic, 'Responses');
    const hordeDir = join(OUTPUT_BASE, topic, 'Responses');
    mkdirSync(hordeDir, { recursive: true });

    // Also need RecordData.yaml for the topic folder
    const vanillaTopicRecord = join(SOURCE_BASE, topic, 'RecordData.yaml');
    try {
        const topicContent = readFileSync(vanillaTopicRecord, 'utf8');
        writeFileSync(join(OUTPUT_BASE, topic, 'RecordData.yaml'), topicContent);
    } catch (e) {
        // RecordData might not exist for all topics
    }

    const files = readdirSync(vanillaDir);
    for (const file of files) {
        const vanilla = readFileSync(join(vanillaDir, file), 'utf8');
        const modified = addHordeCondition(vanilla);
        writeFileSync(join(hordeDir, file), modified);
        total++;
    }
    console.log(`${topic}: ${files.length} INFOs`);
}
console.log(`\nTotal: ${total} override files written to ${OUTPUT_BASE}`);
