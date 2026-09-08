#!/usr/bin/env node
// Build OFFLINE terrain-decoder fixtures from an owner-supplied retail ROM.
// These are synthetic relocated meshes, not gameplay captures or save states.
// Output contains game data: keep local; never package or distribute it.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {inflateRawSync} from 'node:zlib';

const [romPath, catalogPath, output] = process.argv.slice(2);
if (!romPath || !catalogPath || !output) throw new Error('Usage: node export_terrain_audit.mjs ROM DECOMP_EXTRACTION_CATALOG OUTPUT');
const rom = fs.readFileSync(romPath);
const catalog = JSON.parse(fs.readFileSync(catalogPath, 'utf8'));
const sha1 = crypto.createHash('sha1').update(rom).digest('hex');
const version = catalog['inputs-supported'].find(x => x.sha1 === sha1);
if (!version) throw new Error('Unsupported ROM identity / byte order');
const lut = version.assets;
const base = lut + (rom.readUInt32BE(lut) + 2) * 4;
const section = name => {
    const index = catalog.sections.findIndex(x => x['build-id'] === name);
    if (index < 0) throw new Error(`Missing section: ${name}`);
    return rom.subarray(base + rom.readUInt32BE(lut + (index + 1) * 4), base + rom.readUInt32BE(lut + (index + 2) * 4));
};
const entry = (data, table, index) => {
    const begin = table.readUInt32BE(index * 4), end = table.readUInt32BE((index + 1) * 4);
    if (end < begin || end > data.length) throw new Error(`Invalid asset ${index}`);
    return data.subarray(begin, end);
};
const decompress = bytes => {
    const result = inflateRawSync(bytes.subarray(5));
    if (result.length !== bytes.readUInt32LE(0)) throw new Error('Inflated size mismatch');
    return result;
};
const headers = section('ASSET_LEVEL_HEADERS'), headerTable = section('ASSET_LEVEL_HEADERS_TABLE');
const models = section('ASSET_LEVEL_MODELS'), modelTable = section('ASSET_LEVEL_MODELS_TABLE');
const textureData = section('ASSET_TEXTURES_3D'), textureTable = section('ASSET_TEXTURES_3D_TABLE');
const levelNames = catalog.files.filter(x => x.type === 'LevelHeader').slice(0, 65).map(x => x['build-id'].replace('ASSET_LEVEL_', ''));
const MODEL = 0x80100000, CACHE = 0x80080000, TEXTURES = 0x80300000;
const textureCache = new Map();
function texture(id) {
    if (textureCache.has(id)) return textureCache.get(id);
    let bytes = entry(textureData, textureTable, id);
    bytes = Buffer.from(bytes[0x1d] ? decompress(bytes.subarray(32)) : bytes);
    let flags = bytes.readUInt16BE(6);
    const fmt = bytes[2] & 15, mode = bytes[2] >> 4;
    // material_init marks alpha-bearing formats/render modes as translucent.
    if (!(flags & 0x400) && ([4, 5, 6].includes(fmt) || [0, 2].includes(mode))) flags |= 4;
    bytes.writeUInt16BE(flags, 6);
    textureCache.set(id, bytes);
    return bytes;
}
fs.mkdirSync(output, {recursive: true});
const manifest = [];
for (let level = 0; level < 65; ++level) {
    const header = entry(headers, headerTable, level);
    const modelId = header.readInt16BE(0x34);
    if (modelId < 0) continue;
    const raw = decompress(entry(models, modelTable, modelId));
    const ram = Buffer.alloc(0x800000), offset = MODEL & 0x7fffff;
    raw.copy(ram, offset);
    const u32 = a => ram.readUInt32BE(a & 0x7fffff), u16 = a => ram.readUInt16BE(a & 0x7fffff);
    const w32 = (a, v) => ram.writeUInt32BE(v >>> 0, a & 0x7fffff);
    const relocate = a => w32(a, u32(a) + MODEL);
    for (const a of [0, 4, 8, 12, 16, 20]) relocate(MODEL + a);
    const tex = u32(MODEL), segs = u32(MODEL + 4), nt = u16(MODEL + 0x18), ns = u16(MODEL + 0x1a);
    const identities = [], loaded = new Map();
    let nextTexture = TEXTURES;
    for (let i = 0; i < nt; ++i) {
        const id = u32(tex + i * 8) & 0x7fff;
        if (!loaded.has(id)) {
            const bytes = texture(id);
            if ((nextTexture & 0x7fffff) + bytes.length > ram.length) throw new Error('Texture audit heap exhausted');
            bytes.copy(ram, nextTexture & 0x7fffff);
            const slot = loaded.size;
            w32(CACHE + slot * 8, id | 0x8000);
            w32(CACHE + slot * 8 + 4, nextTexture);
            loaded.set(id, nextTexture);
            nextTexture += (bytes.length + 15) & ~15;
        }
        w32(tex + i * 8, loaded.get(id));
        identities.push({id, surface: ram[((tex + i * 8) & 0x7fffff) + 7], triangles: 0});
    }
    for (let si = 0; si < ns; ++si) {
        const s = segs + si * 0x44;
        for (const a of [0, 4, 8, 12]) relocate(s + a);
        const vertices = u32(s), batches = u32(s + 12), nb = u16(s + 0x20);
        // Reproduce generate_track's special authored vertex-alpha convention.
        for (let bi = 0; bi < nb; ++bi) {
            const batch = batches + bi * 12, ti = ram[batch & 0x7fffff];
            if (ti < nt) identities[ti].triangles += u16(batch + 16) - u16(batch + 4);
            for (let vi = u16(batch + 2); vi < u16(batch + 14); ++vi) {
                const v = (vertices + vi * 10) & 0x7fffff;
                if (ram[v + 6] === 1 && ram[v + 7] === 1) {
                    ram[v + 9] = ram[v + 8];
                    ram.fill(128, v + 6, v + 9);
                    w32(batch + 8, u32(batch + 8) | 0x08000000);
                }
            }
        }
    }
    const name = levelNames[level], file = path.resolve(output, `${level}-${name}.bin`);
    ram.swap32();
    fs.writeFileSync(file, ram);
    manifest.push({level, name, modelId, raceType: header[0x4c], file, model: MODEL.toString(16), cache: CACHE.toString(16), count: loaded.size, identities});
}
fs.writeFileSync(path.join(output, 'manifest.json'), JSON.stringify({revision: version.dkr_version, sha1, synthetic: true, levels: manifest}, null, 2) + '\n');
console.log(`Exported ${manifest.length} synthetic level fixtures and ${textureCache.size} unique texture identities for ${version.dkr_version}.`);
