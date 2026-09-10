// Verify the compiled recruitment override, optionally checking that every
// existing record in a previous Horde plugin was preserved.
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { inflateSync } from "node:zlib";
import { fileURLToPath } from "node:url";

const questID = 0x000750ba;
const starterItems = new Set([0x0010e2dd, 0x0010e2de]);
const candidatePath = process.argv[2] ?? fileURLToPath(new URL("../../plugin/Horde.esp", import.meta.url));

function readRecords(path) {
    const bytes = readFileSync(path);
    const records = new Map();
    function walk(start, end) {
        let pos = start;
        while (pos < end) {
            assert.ok(pos + 24 <= end, `Truncated record in ${path}`);
            const tag = bytes.toString("ascii", pos, pos + 4);
            const size = bytes.readUInt32LE(pos + 4);
            if (tag === "GRUP") {
                assert.ok(size >= 24 && pos + size <= end, "Invalid group size");
                walk(pos + 24, pos + size);
                pos += size;
                continue;
            }
            assert.ok(pos + 24 + size <= end, "Invalid record size");
            const flags = bytes.readUInt32LE(pos + 8);
            const id = bytes.readUInt32LE(pos + 12);
            let data = bytes.subarray(pos + 24, pos + 24 + size);
            if (flags & 0x40000) {
                const length = data.readUInt32LE(0);
                data = inflateSync(data.subarray(4));
                assert.equal(data.length, length, "Invalid decompressed size");
            }
            const key = `${tag}:${id}`;
            assert.ok(!records.has(key), `Duplicate record ${key}`);
            records.set(key, { tag, id, flags, data });
            pos += 24 + size;
        }
    }
    walk(0, bytes.length);
    return records;
}

function subrecords(data) {
    const result = [];
    let extendedSize = null;
    for (let pos = 0; pos < data.length;) {
        assert.ok(pos + 6 <= data.length, "Truncated subrecord");
        const tag = data.toString("ascii", pos, pos + 4);
        const size = extendedSize ?? data.readUInt16LE(pos + 4);
        extendedSize = null;
        assert.ok(pos + 6 + size <= data.length, "Invalid subrecord size");
        const value = data.subarray(pos + 6, pos + 6 + size);
        if (tag === "XXXX") {
            assert.equal(size, 4);
            extendedSize = value.readUInt32LE(0);
        } else {
            result.push({ tag, value });
        }
        pos += 6 + size;
    }
    return result;
}

const candidate = readRecords(candidatePath);
const header = candidate.get("TES4:0");
assert.ok(header.flags & 0x200, "Horde must remain ESL-flagged");
assert.deepEqual(subrecords(header.data).filter(s => s.tag === "MAST")
    .map(s => s.value.toString("utf8").replace(/\0$/, "")), ["Skyrim.esm"],
    "The fix must not add a new master dependency");

const key = `QUST:${questID}`;
const quest = candidate.get(key);
assert.ok(quest, "Horde must override DialogueFollower to prevent the starter equipment grant");
const parts = subrecords(quest.data);
assert.ok(parts.some(s => s.tag === "EDID" && s.value.toString("utf8") === "DialogueFollower\0"));
assert.ok(parts.some(s => s.tag === "ALST" && s.value.readUInt32LE(0) === 0), "Follower alias is missing");
assert.ok(parts.some(s => s.tag === "ALST" && s.value.readUInt32LE(0) === 1), "Animal alias is missing");
let aliasID = null;
for (const part of parts) {
    if (part.tag === "ALST") aliasID = part.value.readUInt32LE(0);
    if (part.tag === "ALED") aliasID = null;
    if (aliasID === 0 && part.tag === "CNTO") {
        assert.ok(!starterItems.has(part.value.readUInt32LE(0)), "Follower alias still grants a starter bow or arrows");
    }
}

if (process.argv[3]) {
    const before = readRecords(process.argv[3]);
    assert.equal(candidate.size, before.size + (before.has(key) ? 0 : 1), "Unexpected record additions/removals");
    for (const [id, record] of before) {
        if (id === "TES4:0" || id === key) continue;
        assert.deepEqual(candidate.get(id), record, `Unrelated Horde record changed: ${id}`);
    }
    console.log("All existing Horde record payloads and flags are unchanged.");
}

console.log("Horde recruitment inventory checks passed.");
