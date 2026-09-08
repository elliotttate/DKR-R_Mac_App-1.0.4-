#!/usr/bin/env node
// Validate every owner-ROM mesh fixture at every terrain quality setting.
import fs from 'node:fs';
import {spawnSync} from 'node:child_process';
const [manifestPath, test] = process.argv.slice(2);
if (!manifestPath || !test) throw new Error('Usage: node audit_all_terrain.mjs MANIFEST TEST_EXECUTABLE');
const manifest = JSON.parse(fs.readFileSync(manifestPath, 'utf8'));
if (!manifest.synthetic || manifest.levels.length !== 65) throw new Error('Expected the complete 65-entry ROM level fixture catalog');
let failures = 0;
for (const level of manifest.levels) {
    for (const quality of [0, 1, 2]) {
        const result = spawnSync(test, [level.file, level.model, level.cache, String(level.count), String(quality), String(level.level)], {encoding: 'utf8'});
        const stats = result.stdout.match(/Captured scene: (\d+) source \/ (\d+) selected \/ (\d+) detail triangles; (\d+) coincident samples; (\d+) decoration triangles/);
        if (result.status !== 0 || !stats) {
            console.error(`FAIL ${level.name} quality ${quality}: ${result.stdout}\n${result.stderr}`);
            ++failures;
            continue;
        }
        if ([0, 3, 8, 64, 65, 66].includes(level.raceType) && +stats[2] === 0) {
            console.error(`MISSING COVERAGE: ${level.name}`);
            ++failures;
        }
        console.log(JSON.stringify({level: level.level, name: level.name, quality, source: +stats[1], selected: +stats[2], triangles: +stats[3], shared: +stats[4], decorations: +stats[5]}));
    }
}
console.error(`${manifest.revision}: ${manifest.levels.length * 3} mesh/quality cases; ${failures} failures.`);
process.exitCode = failures ? 1 : 0;
