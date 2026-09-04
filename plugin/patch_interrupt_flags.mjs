/**
 * Patches InterruptFlags in Horde.esp after Spriggit deserialize.
 *
 * Spriggit/Mutagen only serializes named PackageInterruptFlag enum values
 * (bits 0-8). Unnamed bits 9-15 are silently dropped during YAML round-trip.
 * Without these bits, sandbox packages don't evaluate correctly.
 *
 * This walks the top-level PACK group and ORs 0xFE00 (bits 9-15) into the
 * InterruptFlags uint16 at PKDT data offset +8 of every PACK record.
 *
 * NOTE: an earlier version scanned the whole file for "EDID" byte sequences and
 * then searched 200 bytes forward for a "PKDT" tag. That matched non-package
 * records whose EditorID happened to contain "Sandbox" (the Horde_SandboxFaction
 * FACT, the Horde_IdleSandbox GLOB, the Horde_IdleSandboxMarker REFR) and could
 * in principle have written into a neighbouring record. Proper group traversal
 * removes that whole class of failure.
 */

import { readFileSync, writeFileSync } from "fs";

const ESP_PATH = new URL("./Horde.esp", import.meta.url).pathname.replace(/^\/([A-Z]:)/, "$1");

const HIGH_BITS = 0xfe00; // bits 9-15
const HEADER_LEN = 24;

const buf = readFileSync(ESP_PATH);

function tag(offset) {
  return buf.toString("ascii", offset, offset + 4);
}

/** Read a record's EDID subrecord, or null. */
function readEditorID(recordStart, dataSize) {
  let p = recordStart + HEADER_LEN;
  const end = p + dataSize;
  while (p + 6 <= end) {
    const subTag = tag(p);
    const subSize = buf.readUInt16LE(p + 4);
    if (subTag === "EDID") {
      return buf.toString("ascii", p + 6, p + 6 + subSize).replace(/\0$/, "");
    }
    p += 6 + subSize;
  }
  return null;
}

/** Find the PKDT subrecord's data offset within a record, or -1. */
function findPKDT(recordStart, dataSize) {
  let p = recordStart + HEADER_LEN;
  const end = p + dataSize;
  while (p + 6 <= end) {
    const subTag = tag(p);
    const subSize = buf.readUInt16LE(p + 4);
    if (subTag === "PKDT") return p + 6;
    p += 6 + subSize;
  }
  return -1;
}

// Walk top-level groups looking for PACK.
let patched = 0;
let seen = 0;
let pos = buf.readUInt32LE(4) + HEADER_LEN; // skip the TES4 header record

while (pos + HEADER_LEN <= buf.length) {
  if (tag(pos) !== "GRUP") break;

  const groupSize = buf.readUInt32LE(pos + 4);
  const label = buf.toString("ascii", pos + 8, pos + 12);
  const groupType = buf.readUInt32LE(pos + 12);

  if (groupType === 0 && label === "PACK") {
    let rec = pos + HEADER_LEN;
    const groupEnd = pos + groupSize;

    while (rec + HEADER_LEN <= groupEnd) {
      if (tag(rec) === "GRUP") {
        rec += buf.readUInt32LE(rec + 4);
        continue;
      }

      const dataSize = buf.readUInt32LE(rec + 4);
      const recordFlags = buf.readUInt32LE(rec + 8);

      if (recordFlags & 0x00040000) {
        console.warn(`  WARN: compressed PACK at 0x${rec.toString(16)} — skipped`);
        rec += HEADER_LEN + dataSize;
        continue;
      }

      const editorID = readEditorID(rec, dataSize) ?? `<no EDID @0x${rec.toString(16)}>`;
      const pkdt = findPKDT(rec, dataSize);
      seen++;

      if (pkdt === -1) {
        console.warn(`  WARN: ${editorID} has no PKDT — skipped`);
      } else {
        const offset = pkdt + 8;
        const before = buf.readUInt16LE(offset);
        const after = before | HIGH_BITS;
        const hex = (v) => `0x${v.toString(16).toUpperCase().padStart(4, "0")}`;

        if (before !== after) {
          buf.writeUInt16LE(after, offset);
          console.log(`  ${editorID}: InterruptFlags ${hex(before)} → ${hex(after)}`);
          patched++;
        } else {
          console.log(`  ${editorID}: already has high bits (${hex(before)})`);
        }
      }

      rec += HEADER_LEN + dataSize;
    }
  }

  pos += groupSize;
}

console.log(`\nScanned ${seen} PACK record(s).`);
if (patched > 0) {
  writeFileSync(ESP_PATH, buf);
  console.log(`Patched ${patched} package(s) in Horde.esp`);
} else {
  console.log("No packages needed patching.");
}
