// Synthetic PE section for compatibility tests. Never used by production code.
function compatibleFixture(original) {
  const bytes = original ? Buffer.from(original) : Buffer.alloc(512);
  if (!original) {
    bytes.writeUInt16LE(0x5a4d); bytes.writeUInt32LE(64, 60);
    bytes.writeUInt32LE(0x4550, 64); bytes.writeUInt16LE(0x14c, 68);
    bytes.writeUInt16LE(224, 84); bytes.writeUInt16LE(0x10b, 88);
    bytes.writeUInt32LE(0x400000, 116); bytes.writeUInt32LE(512, 148);
  }
  const pe = bytes.readUInt32LE(60), opt = pe + 24;
  const count = bytes.readUInt16LE(pe + 6);
  if (original) {
    // Extend the last section: Windows rejects an artificial section with an
    // unmapped gap before it. Preserve all original code and debug data.
    const last = opt + bytes.readUInt16LE(pe + 20) + (count - 1) * 40;
    const va = bytes.readUInt32LE(last + 12), raw = bytes.readUInt32LE(last + 20);
    const size = 0x425000 - va;
    if (size <= 0) throw new Error('Probe fixture image is unexpectedly large');
    const result = Buffer.alloc(Math.max(bytes.length, raw + size)); bytes.copy(result);
    result.writeUInt32LE(size, last + 8); result.writeUInt32LE(size, last + 16);
    result.writeUInt32LE(0x425000, opt + 56);
    result.writeUInt32LE(0x94bf, raw + 0x42457c - va);
    return result;
  }
  const header = opt + bytes.readUInt16LE(pe + 20) + count * 40;
  if (header + 40 > bytes.readUInt32LE(opt + 60)) throw new Error('No spare PE section header for probe fixture');
  const offset = Math.ceil(bytes.length / 512) * 512;
  const result = Buffer.alloc(offset + 4096); bytes.copy(result);
  result.writeUInt16LE(count + 1, pe + 6);
  result.write('.test', header); result.writeUInt32LE(4096, header + 8);
  result.writeUInt32LE(0x424000, header + 12); result.writeUInt32LE(4096, header + 16);
  result.writeUInt32LE(offset, header + 20); result.writeUInt32LE(0x40000040, header + 36);
  result.writeUInt32LE(Math.max(result.readUInt32LE(opt + 56), 0x425000), opt + 56);
  result.writeUInt32LE(0x94bf, offset + 0x57c);
  return result;
}
module.exports = { compatibleFixture };
