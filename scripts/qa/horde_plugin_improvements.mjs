// Validate the actual compiled ESP, including condition AND/OR behavior.
// Usage: node scripts/qa/horde_plugin_improvements.mjs [candidate.esp] [baseline.esp]
import assert from 'node:assert/strict';
import { fileURLToPath } from 'node:url';
import { readRecords, subrecords, zstring } from './esp_records.mjs';

const path = process.argv[2] ?? fileURLToPath(new URL('../../plugin/Horde.esp', import.meta.url));
const records = readRecords(path);
const header = records.get('TES4:0');
assert.ok(header.flags & 0x200, 'Must remain ESL-flagged');
const masters = subrecords(header.data).filter(s => s.tag === 'MAST').map(s => zstring(s.value));
assert.deepEqual(masters, ['Skyrim.esm'], 'No unused master dependencies');
const local = id => 0x01000000 + id;
for (const record of records.values()) {
    if ((record.id >>> 24) === masters.length) {
        const id = record.id & 0xffffff;
        assert.ok(id >= 0x800 && id <= 0xfff, `Invalid ESL local ID: ${id.toString(16)}`);
    }
}
function parts(tag, id) {
    const record = records.get(`${tag}:${id}`);
    assert.ok(record, `Missing ${tag}:${id.toString(16)}`);
    return subrecords(record.data);
}
function field(parts, tag) {
    const value = parts.find(s => s.tag === tag)?.value;
    assert.ok(value, `Missing subrecord ${tag}`);
    return value;
}
const stack = [0x839, 0x830, 0x803, 0x802].map(local);
const quest = parts('QUST', local(0x805));
let alias = null;
const aliases = [];
for (const part of quest) {
    if (part.tag === 'ALST') {
        alias = { id: part.value.readUInt32LE(), packages: [] };
        aliases.push(alias);
    }
    if (part.tag === 'ALED') alias = null;
    if (!alias) continue;
    if (part.tag === 'FNAM') alias.flags = part.value.readUInt32LE();
    if (part.tag === 'ALPC') alias.packages.push(part.value.readUInt32LE());
}
assert.equal(aliases.length, 20);
for (const [index, a] of aliases.entries()) {
    assert.equal(a.id, index, 'Existing alias IDs must remain stable');
    assert.equal(a.flags, 0x302, 'Optional, StoresText, AllowReserved; no unconditional protection');
    assert.deepEqual(a.packages, stack, `Wrong package order in alias ${index}`);
}
assert.equal(field(parts('GLOB', local(0x838)), 'FLTV').readFloatLE(), 0, 'Sandbox defaults off');
assert.equal(zstring(field(parts('FACT', local(0x83a)), 'EDID')), 'Horde_SandboxEnabledFaction');

const packs = stack.map(id => parts('PACK', id));
for (const p of packs) {
    assert.equal(field(p, 'PKDT').readUInt16LE(8) & 0xfe00, 0xfe00, 'Interrupt flags lost in rebuild');
}
assert.equal(field(packs[1], 'PKCU').readUInt32LE(4), 0x6c872, 'Active template must be SandboxAndKeepEyeOn');
assert.equal(field(packs[0], 'PKCU').readUInt32LE(4), 0x68b86, 'Waiting template must be SandboxMultiLocation');
for (const p of packs.slice(0, 2)) {
    assert.ok(field(p, 'PKDT').readUInt32LE() & 0x200000, 'Sandbox should unequip weapons');
}
const activeLocations = packs[1].filter(s => s.tag === 'PLDT').map(s => s.value);
assert.equal(activeLocations.length, 3);
assert.deepEqual(activeLocations.map(p => [p.readUInt32LE(), p.readUInt32LE(4), p.readUInt32LE(8)]),
    [[0, 0x14, 800], [0, 0x14, 0], [0, 0x14, 800]], 'Active sandbox must target the player, never the legacy marker');
const waitLocation = field(packs[0], 'PLDT');
assert.deepEqual([waitLocation.readUInt32LE(), waitLocation.readUInt32LE(4), waitLocation.readUInt32LE(8)],
    [12, 0, 800], 'Waiting sandbox must use NearSelf at 800 units');
assert.ok(!packs[0].some(s => s.tag === 'PTDA'), 'Waiting must not pursue the player');

const safeKeywords = [0x13168, 0x1cd5a, 0x1cb87, 0xfc1a3, 0x13167, 0x1cb86, 0x13166];
function conditions(p) {
    return p.filter(s => s.tag === 'CTDA').map(({ value: b }) => {
        assert.equal(b.length, 32);
        assert.equal(b[0] & ~1, 0, 'These package conditions must use float equality, with optional OR');
        return { or: !!(b[0] & 1), compare: b.readFloatLE(4), fn: b.readUInt16LE(8),
            param: b.readUInt32LE(12), runOn: b.readUInt32LE(20), reference: b.readUInt32LE(24) };
    });
}
const packageConditions = packs.map(conditions);
for (const list of packageConditions.slice(0, 2)) {
    assert.equal(list.at(-1).or, false, 'Final OR group must terminate');
    assert.deepEqual(list.filter(c => c.fn === 562).map(c => c.param), safeKeywords);
}
function conditionValue(c, state) {
    switch (c.fn) {
        case 71: // GetInFaction
            assert.equal(c.param, local(0x83a)); assert.equal(c.runOn, 0);
            return +state.optIn;
        case 74: // GetGlobalValue
            assert.equal(c.param, local(0x838)); assert.equal(c.runOn, 0);
            return +state.global;
        case 453: // GetPlayerTeammate
            assert.equal(c.runOn, 0); return +state.teammate;
        case 14: // GetActorValue WaitingForPlayer
            assert.equal(c.param, 95); assert.equal(c.runOn, 0); return +state.waiting;
        case 286: // IsSneaking on player
            assert.equal(c.runOn, 2); assert.equal(c.reference, 0x14); return +state.sneaking;
        case 289: // IsInCombat on player or subject
            assert.ok(c.runOn === 0 || c.runOn === 2);
            if (c.runOn === 2) assert.equal(c.reference, 0x14);
            return +(c.runOn === 2 ? state.playerCombat : state.actorCombat);
        case 562: // LocationHasKeyword on follower's location
            assert.equal(c.runOn, 0); return +(state.location === c.param);
        default: assert.fail(`Unexpected condition function ${c.fn}`);
    }
}
function eligible(list, state) {
    let group = false;
    for (const c of list) {
        // Evaluate even if a prior OR member passed, so every field is checked.
        const matches = conditionValue(c, state) === c.compare;
        group = group || matches;
        if (!c.or) {
            if (!group) return false;
            group = false;
        }
    }
    return true;
}
let cases = 0;
for (let mask = 0; mask < 128; mask++) {
    for (const location of [0, ...safeKeywords]) {
        const state = { location };
        ['global', 'optIn', 'teammate', 'waiting', 'sneaking', 'playerCombat', 'actorCombat']
            .forEach((key, bit) => { state[key] = !!(mask & (1 << bit)); });
        const canSandbox = state.global && state.optIn && state.teammate && location !== 0 && !state.actorCombat;
        const expected = state.waiting ? (canSandbox ? 0 : 2)
            : (canSandbox && !state.sneaking && !state.playerCombat ? 1 : 3);
        const selected = packageConditions.findIndex(p => eligible(p, state));
        assert.equal(selected, expected, `Incorrect package choice for ${JSON.stringify(state)}`);
        cases++;
    }
}

if (process.argv[3]) {
    const baseline = readRecords(process.argv[3]);
    const changed = new Set(['TES4:0', `QUST:${local(0x805)}`, `PACK:${local(0x830)}`]);
    const added = new Set([`GLOB:${local(0x838)}`, `PACK:${local(0x839)}`, `FACT:${local(0x83a)}`,
        `QUST:${local(0x83b)}`, `PACK:${local(0x83c)}`]);
    assert.equal(records.size, baseline.size + added.size);
    for (const [key, record] of baseline) {
        assert.ok(records.has(key), `Existing record removed: ${key}`);
        if (!changed.has(key)) assert.deepEqual(records.get(key), record, `Unrelated record changed: ${key}`);
    }
    for (const key of records.keys()) assert.ok(baseline.has(key) || added.has(key), `Unexpected new record ${key}`);
    console.log('Existing IDs, dialogue, recruitment quest, factions, powers, wait/follow packages, and marker preserved.');
}
console.log(`Horde plugin improvements passed: 20 aliases, ESL bounds, package data, ${cases} compiled-condition scenarios.`);
