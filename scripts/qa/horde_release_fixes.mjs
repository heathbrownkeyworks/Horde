// Inspect the rebuilt residence records and, optionally, preserve every pre-fix record.
// Usage: node scripts/qa/horde_release_fixes.mjs candidate.esp [pre-fix.esp]
import assert from 'node:assert/strict';
import { fileURLToPath } from 'node:url';
import { readRecords, subrecords, zstring } from './esp_records.mjs';

const file = process.argv[2] ?? fileURLToPath(new URL('../../plugin/Horde.esp', import.meta.url));
const records = readRecords(file);
const fields = (type, id) => {
    const record = records.get(`${type}:${id}`);
    assert.ok(record, `Missing ${type}:${id.toString(16)}; rebuild Horde.esp`);
    return subrecords(record.data);
};
const field = (parts, name) => {
    const value = parts.find(p => p.tag === name)?.value;
    assert.ok(value, `Missing ${name}`);
    return value;
};
const quest = fields('QUST', 0x0100083b);
assert.equal(zstring(field(quest, 'EDID')), 'Horde_ResidenceQuest');
assert.equal(field(quest, 'DNAM')[2], 1, 'Residence must yield to higher priority quests');
assert.ok(field(quest, 'DNAM').readUInt16LE() & 1, 'Residence quest must start automatically');
assert.ok(!quest.some(p => p.tag === 'ALST'), 'Residence must not consume finite follower aliases');
const home = fields('PACK', 0x0100083c);
assert.equal(zstring(field(home, 'EDID')), 'Horde_ResidencePkg');
assert.equal(field(home, 'PKCU').readUInt32LE(4), 0x1c254, 'Use the vanilla Sandbox template');
const location = field(home, 'PLDT');
assert.deepEqual([location.readUInt32LE(), location.readUInt32LE(4), location.readUInt32LE(8)], [3,0,512],
    'Home location must use each actor\'s editor location, with no shared marker');
assert.equal(field(home, 'PKDT').readUInt16LE(8) & 0xfe00, 0xfe00, 'Preserve package interrupts');
const conditions = home.filter(p => p.tag === 'CTDA').map(p => p.value);
assert.equal(conditions.length, 2);
assert.deepEqual(conditions.map(p => [p[0],p.readFloatLE(4),p.readUInt16LE(8),p.readUInt32LE(12),p.readUInt32LE(20)]),
    [[0,0,453,0,0],[0,0,71,0x5c84e,0]], 'Home applies only outside teammate and CurrentFollowerFaction state');
if (process.argv[3]) {
    const before=readRecords(process.argv[3]);
    const added=new Set(['QUST:16779323','PACK:16779324']);
    assert.equal(records.size,before.size+2);
    for(const [key,record] of before) {
        assert.ok(records.has(key), `Removed pre-fix record ${key}`);
        if(key !== 'TES4:0') assert.deepEqual(records.get(key),record,`Unrelated pre-fix record changed: ${key}`);
    }
    for(const key of records.keys()) assert.ok(before.has(key) || added.has(key),`Unexpected added record ${key}`);
}
console.log('Horde release repair ESP checks passed: independent low-priority homes and preserved existing records.');
