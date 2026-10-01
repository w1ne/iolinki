/**
 * IODD `<Stamp crc="...">` checksum — the IO-Link IODD stamp algorithm.
 *
 * Zero dependencies. Pure ES module. No Node built-ins, so this runs unchanged
 * in browsers, Bun, Node and Deno.
 *
 * ---------------------------------------------------------------------------
 * ALGORITHM (IODD Specification 10.012, "Stamp" element)
 *
 *   "For the CRC, the CRC-32 algorithm is used (see section 8.1.1.6.2 of ITU-T
 *    recommendation V.42 (03/2002) or ISO/IEC 13239:2002). Before the actual
 *    calculation, the 'crc' attribute is set to an empty string and the checker
 *    inserts its name and version into the appropriate attributes.
 *
 *    The IODD file is read in binary mode. The stream of bytes is fed into the
 *    CRC algorithm until the string `<Stamp crc="` has been processed. The value
 *    of the attribute crc is skipped, and the CRC calculation continues with the
 *    closing quotation mark.
 *
 *    The same is done with external language documents, but after the
 *    end-of-file has been reached, the CRC of the main IODD file is converted to
 *    decimal representation (no leading zeroes) and the character codes for the
 *    digits are fed into the CRC algorithm."
 *
 * ---------------------------------------------------------------------------
 * PRACTICAL NOTES (learned the hard way from a 40-file corpus)
 *
 *  - ITU-T V.42 CRC-32 is exactly the ordinary zlib/PNG CRC-32
 *    (CRC-32/ISO-HDLC): poly 0x04C11DB7, reflected 0xEDB88320,
 *    init 0xFFFFFFFF, refin + refout, xorout 0xFFFFFFFF.
 *  - "Binary mode" is literal. Hash the bytes exactly as they sit on disk:
 *      * do NOT normalise CRLF -> LF,
 *      * do NOT strip a UTF-8 BOM (real vendor IODDs have one, and it counts),
 *      * do NOT add or trim a trailing newline.
 *  - Only `<ExternalTextDocument>` roots get the appended main-IODD CRC digits.
 *    `IODevice`, `IODDStandardDefinitions` and `IODDStandardUnitDefinitions`
 *    do not.
 *  - The result is an unsigned 32-bit integer, serialised in decimal.
 */

// ----------------------------------------------------------------- CRC core

/** Precomputed reflected CRC-32 table (poly 0xEDB88320). */
const CRC_TABLE = /* @__PURE__ */ (() => {
  const table = new Uint32Array(256);
  for (let n = 0; n < 256; n++) {
    let c = n;
    for (let k = 0; k < 8; k++) {
      c = c & 1 ? (0xedb88320 ^ (c >>> 1)) >>> 0 : c >>> 1;
    }
    table[n] = c >>> 0;
  }
  return table;
})();

/**
 * Streaming CRC-32 (ISO-HDLC / zlib).
 *
 * @example
 * const c = new Crc32();
 * c.update(bytesA).update(bytesB);
 * console.log(c.value);
 */
export class Crc32 {
  constructor() {
    this._crc = 0xffffffff;
  }

  /**
   * Feed bytes into the running checksum.
   * @param {Uint8Array} bytes
   * @returns {Crc32} this, for chaining
   */
  update(bytes) {
    let crc = this._crc;
    for (let i = 0; i < bytes.length; i++) {
      crc = (CRC_TABLE[(crc ^ bytes[i]) & 0xff] ^ (crc >>> 8)) >>> 0;
    }
    this._crc = crc;
    return this;
  }

  /** @returns {number} the current CRC as an unsigned 32-bit integer */
  get value() {
    return (this._crc ^ 0xffffffff) >>> 0;
  }

  /** Reset to the initial state. */
  reset() {
    this._crc = 0xffffffff;
    return this;
  }
}

/**
 * One-shot CRC-32 (ISO-HDLC / zlib).
 *
 * Check value: `crc32(new TextEncoder().encode('123456789')) === 0xCBF43926`.
 *
 * @param {Uint8Array} bytes
 * @returns {number} unsigned 32-bit integer
 */
export function crc32(bytes) {
  return new Crc32().update(bytes).value;
}

// -------------------------------------------------------------- byte helpers

const MARKER = '<Stamp crc="';
const encoder = /* @__PURE__ */ new TextEncoder();
const decoder = /* @__PURE__ */ new TextDecoder();
const MARKER_BYTES = /* @__PURE__ */ encoder.encode(MARKER);
const QUOTE = 0x22; // '"'

function indexOfBytes(haystack, needle, from = 0) {
  outer: for (let i = from; i <= haystack.length - needle.length; i++) {
    for (let j = 0; j < needle.length; j++) {
      if (haystack[i + j] !== needle[j]) continue outer;
    }
    return i;
  }
  return -1;
}

/**
 * Locate the `crc` attribute value within the raw bytes.
 *
 * @param {Uint8Array} bytes
 * @returns {{markerStart:number, valueStart:number, valueEnd:number, value:string}|null}
 *   `valueStart` is the index of the first digit; `valueEnd` is the index of the
 *   closing quotation mark. Returns null when no stamp marker is present.
 */
export function findStampCrc(bytes) {
  const markerStart = indexOfBytes(bytes, MARKER_BYTES);
  if (markerStart < 0) return null;

  const valueStart = markerStart + MARKER_BYTES.length;
  let valueEnd = valueStart;
  while (valueEnd < bytes.length && bytes[valueEnd] !== QUOTE) valueEnd++;
  if (valueEnd >= bytes.length) return null;

  return {
    markerStart,
    valueStart,
    valueEnd,
    value: decoder.decode(bytes.subarray(valueStart, valueEnd)),
  };
}

/**
 * True when the document's root element is `<ExternalTextDocument>`, which is
 * the only variant requiring the main IODD's CRC to be appended.
 *
 * @param {Uint8Array} bytes
 */
export function isExternalTextDocument(bytes) {
  const head = decoder.decode(bytes.subarray(0, Math.min(bytes.length, 8192)));
  return /<ExternalTextDocument[\s>]/.test(head);
}

// ----------------------------------------------------------------------- API

/**
 * Compute the Stamp CRC for an IODD document.
 *
 * @param {Uint8Array} bytes
 *   Raw file bytes exactly as they will be written to disk, including any BOM
 *   and with real line endings. A `<Stamp crc="...">` element must already be
 *   present; its current value is skipped, not hashed.
 * @param {object} [options]
 * @param {number|null} [options.mainIoddCrc]
 *   For an `<ExternalTextDocument>`, the CRC of the main IODD file. Its decimal
 *   digits are appended to the byte stream. Omit for other document types.
 * @returns {number} unsigned 32-bit CRC
 * @throws {Error} when no stamp marker is found
 */
export function computeStampCrc(bytes, options = {}) {
  const { mainIoddCrc = null } = options;

  const stamp = findStampCrc(bytes);
  if (!stamp) {
    throw new Error('computeStampCrc: no `<Stamp crc="` marker found in document');
  }

  const crc = new Crc32();
  // Everything up to and including `<Stamp crc="`
  crc.update(bytes.subarray(0, stamp.valueStart));
  // ...the attribute value itself is skipped...
  // ...resume at the closing quotation mark, through to end of file.
  crc.update(bytes.subarray(stamp.valueEnd));

  if (mainIoddCrc !== null && mainIoddCrc !== undefined) {
    crc.update(encoder.encode(String(mainIoddCrc >>> 0)));
  }

  return crc.value;
}

/**
 * Verify a document's existing stamp.
 *
 * @param {Uint8Array} bytes
 * @param {object} [options] see {@link computeStampCrc}
 * @returns {{valid:boolean, stored:number|null, computed:number}}
 */
export function verifyStampCrc(bytes, options = {}) {
  const stamp = findStampCrc(bytes);
  const stored = stamp && /^\d+$/.test(stamp.value) ? Number(stamp.value) : null;
  const computed = computeStampCrc(bytes, options);
  return { valid: stored !== null && stored === computed, stored, computed };
}

/**
 * Return a copy of the document with the Stamp's `crc` attribute set to the
 * freshly computed value.
 *
 * Only the digits inside `crc="..."` are replaced — every other byte, including
 * whitespace, indentation, line endings and any BOM, is preserved exactly. This
 * makes the operation safe for round-tripping vendor files.
 *
 * @param {Uint8Array} bytes
 * @param {object} [options] see {@link computeStampCrc}
 * @returns {{bytes:Uint8Array, crc:number, previous:string|null}}
 */
export function applyStampCrc(bytes, options = {}) {
  const crc = computeStampCrc(bytes, options);
  const stamp = findStampCrc(bytes);

  const digits = encoder.encode(String(crc));
  const out = new Uint8Array(bytes.length - (stamp.valueEnd - stamp.valueStart) + digits.length);

  out.set(bytes.subarray(0, stamp.valueStart), 0);
  out.set(digits, stamp.valueStart);
  out.set(bytes.subarray(stamp.valueEnd), stamp.valueStart + digits.length);

  return { bytes: out, crc, previous: stamp.value };
}

/**
 * Read the `<Checker name="..." version="..."/>` identification from a stamp.
 *
 * @param {Uint8Array} bytes
 * @returns {{name:string, version:string}|null}
 */
export function readChecker(bytes) {
  const stamp = findStampCrc(bytes);
  if (!stamp) return null;
  const tail = decoder.decode(bytes.subarray(stamp.valueEnd, Math.min(bytes.length, stamp.valueEnd + 512)));
  const m = /<Checker\s+name="([^"]*)"\s+version="([^"]*)"/.exec(tail);
  return m ? { name: m[1], version: m[2] } : null;
}
