// Read-only, bounded ESP parser shared by plugin regression checks.
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { inflateSync } from 'node:zlib';

export function readRecords(path) {
    const bytes = readFileSync(path);
    const records = new Map();
    function walk(start, end) {
        let pos = start;
        while (pos < end) {
            assert.ok(pos + 24 <= end, 'Truncated record');
            const tag = bytes.toString('ascii', pos, pos + 4);
            const size = bytes.readUInt32LE(pos + 4);
            if (tag === 'GRUP') {
                assert.ok(size >= 24 && pos + size <= end, 'Invalid group');
                walk(pos + 24, pos + size);
                pos += size;
                continue;
            }
            assert.ok(pos + 24 + size <= end, 'Invalid record size');
            const flags = bytes.readUInt32LE(pos + 8);
            const id = bytes.readUInt32LE(pos + 12);
            let data = bytes.subarray(pos + 24, pos + 24 + size);
            if (flags & 0x40000) {
                const length = data.readUInt32LE(0);
                data = inflateSync(data.subarray(4));
                assert.equal(data.length, length);
            }
            const key = `${tag}:${id}`;
            assert.ok(!records.has(key), `Duplicate record ${key}`);
            records.set(key, { tag, id, flags, data });
            pos += 24 + size;
        }
        assert.equal(pos, end);
    }
    walk(0, bytes.length);
    return records;
}

export function subrecords(data) {
    const result = [];
    let extendedSize = null;
    for (let pos = 0; pos < data.length;) {
        assert.ok(pos + 6 <= data.length, 'Truncated subrecord');
        const tag = data.toString('ascii', pos, pos + 4);
        const size = extendedSize ?? data.readUInt16LE(pos + 4);
        extendedSize = null;
        assert.ok(pos + 6 + size <= data.length, 'Invalid subrecord size');
        const value = data.subarray(pos + 6, pos + 6 + size);
        if (tag === 'XXXX') {
            assert.equal(size, 4);
            extendedSize = value.readUInt32LE(0);
        } else {
            result.push({ tag, value });
        }
        pos += 6 + size;
    }
    assert.equal(extendedSize, null);
    return result;
}

export const zstring = data => data.toString('utf8').replace(/\0$/, '');
