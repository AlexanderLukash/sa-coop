// Match the address/signature required by Core::Init, independently of file size.
function inspectExecutable(bytes) {
  const unsupported = { supported: false, version: 'Невідома або модифікована збірка', reason: 'Адреси коду не відповідають GTA 1.0 US, потрібній цьому моду.' };
  try {
    if (bytes.readUInt16LE(0) !== 0x5a4d) return unsupported;
    const pe = bytes.readUInt32LE(0x3c);
    if (bytes.readUInt32LE(pe) !== 0x4550 || bytes.readUInt16LE(pe + 4) !== 0x14c) return unsupported;
    const optional = pe + 24, size = bytes.readUInt16LE(pe + 20);
    if (size < 96 || bytes.readUInt16LE(optional) !== 0x10b || bytes.readUInt32LE(optional + 28) !== 0x400000) return unsupported;
    const rva = 0x82457c - 0x400000;
    const count = bytes.readUInt16LE(pe + 6);
    if (count > 96) return unsupported;
    for (let i = 0; i < count; i++) {
      const section = optional + size + i * 40;
      const va = bytes.readUInt32LE(section + 12), rawSize = bytes.readUInt32LE(section + 16), raw = bytes.readUInt32LE(section + 20);
      if (rva < va || rva + 4 > va + rawSize) continue;
      const offset = raw + rva - va;
      if (offset + 4 > bytes.length || bytes.readUInt32LE(offset) !== 0x94bf) return unsupported;
      return { supported: true, version: 'GTA 1.0 US (сумісна сигнатура)', reason: '' };
    }
  } catch { /* Truncated/invalid PE files are unsupported. */ }
  return unsupported;
}
module.exports = { inspectExecutable };
