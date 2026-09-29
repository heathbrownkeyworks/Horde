#!/usr/bin/env node
// Copy the winning dismiss/wait/follow INFO records and append a condition
// hiding them for Horde-tracked actors (Horde_FollowerFaction rank 1).
// Use a Spriggit dump of the load-order winner to preserve other mods' edits.
// Usage: node generate_dialogue_overrides.mjs <sourceDialogTopicsDir> [outputDialogTopicsDir]

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
            // The next top-level key ends the conditions block.
            if (line.length > 0 && !line.startsWith(' ') && !line.startsWith('-') && !line.startsWith('\r')) {
                condEnd = i;
                break;
            }
        }
    }

    if (condStart < 0) {
        // Add a conditions block if none exists.
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

    // Preserve the parent topic record when supplied.
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
