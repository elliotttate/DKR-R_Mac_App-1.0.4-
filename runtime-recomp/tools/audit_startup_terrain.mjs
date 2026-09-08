#!/usr/bin/env node
// Offline validation of owner-supplied local RDRAM captures. No game data ships.
import fs from 'node:fs';
import {spawnSync} from 'node:child_process';

const args = Object.fromEntries(Array.from({length: (process.argv.length - 2) / 2}, (_, i) =>
    [process.argv[2 + i * 2], process.argv[3 + i * 2]]));
if (!args['--prefix'] || !args['--log'] || !args['--test'] || !['v77', 'v80'].includes(args['--revision'])) {
    console.error('Usage: node audit_startup_terrain.mjs --prefix CAPTURE_PREFIX --log RUNTIME_LOG --test TEST_EXECUTABLE --revision v77|v80');
    process.exit(2);
}
const courses = new Map([
    [23, 'Opening montage'], [18, 'Greenwood Village'], [28, 'Frosty Village'],
    [7, 'Hot Top Volcano'], [29, 'Jungle Falls'], [19, 'Boulder Canyon'],
    [5, 'Ancient Lake'], [8, 'Whale Bay'], [31, 'Haunted Woods'],
]);
const captured = new Map();
const log = fs.readFileSync(args['--log'], 'utf8');
for (const match of log.matchAll(/\[terrain-dump\] model=([\da-f]+) cache=([\da-f]+) count=(\d+) scene=(\d+)(?: level=(\d+))?/g)) {
    const [, model, cache, count, scene] = match;
    const file = `${args['--prefix']}-${scene}.bin`;
    if (!fs.existsSync(file)) continue;
    const bytes = fs.readFileSync(file);
    if (bytes.length !== 0x800000) throw new Error(`Invalid snapshot size: ${file}`);
    const mapAddress = args['--revision'] === 'v80' ? 0x801216e4 : 0x80121164;
    const level = bytes.readUInt32LE(mapAddress & 0x7fffff);
    if (match[5] && Number(match[5]) !== level) throw new Error(`Level metadata mismatch: ${file}`);
    if (courses.has(level) && !captured.has(level)) captured.set(level, {file, model, cache, count});
}
let missing = false;
for (const [level, name] of courses) {
    const capture = captured.get(level);
    if (!capture) { console.error(`MISSING: ${name} (${level})`); missing = true; continue; }
    for (const quality of [0, 1, 2]) {
        const result = spawnSync(args['--test'], [capture.file, capture.model, capture.cache, capture.count, String(quality), String(level)], {encoding: 'utf8'});
        if (result.status !== 0) throw new Error(`${name}, quality ${quality}: ${result.stdout}\n${result.stderr}`);
        const stats = result.stdout.match(/Captured scene: (\d+) source \/ (\d+) selected \/ (\d+) detail triangles; (\d+) coincident samples; (\d+) decoration triangles/);
        if (!stats || Number(stats[2]) === 0) throw new Error(`No terrain coverage: ${name}`);
        console.log(JSON.stringify({level, name, quality, source: +stats[1], selected: +stats[2], triangles: +stats[3], shared: +stats[4], decorations: +stats[5]}));
    }
}
process.exitCode = missing ? 1 : 0;
