const { spawnSync } = require('child_process');
const fs = require('fs');
const os = require('os');
const path = require('path');
const { existingDir, existingFile, requireSkimBin, resolveOxcEmit, resolveOxcRoot, resolveTypeScriptRoot, root, skip } = require('./support.cjs');

const tsRoot = resolveTypeScriptRoot();
const tsCases = process.env.TS_CASES || (tsRoot ? path.join(tsRoot, 'tests/cases/conformance') : '');
const oxcRoot = resolveOxcRoot();
const oxc = resolveOxcEmit(oxcRoot);
if (!existingDir([tsCases])) {
  skip('TypeScript conformance cases not found. Set TS_CASES or TS_ROOT, clone TypeScript next to this repo, or run `node tests/run_all.cjs` to download deps.');
}
if (!oxc) {
  skip('oxc_emit not found. Set OXC_EMIT, put oxc_emit on PATH, set OXC_ROOT with a built target/release/oxc_emit, or run `node tests/run_all.cjs`.');
}
const runs = Number(process.env.BENCH_RUNS || '5');
const loops = Number(process.env.BENCH_LOOPS || '200');

const cases = [
  ['3.8KB literalTypeWidening', 'types/literal/literalTypeWidening.ts'],
  ['36KB parserindenter', 'parser/ecmascript5/RealWorld/parserindenter.ts'],
  ['68KB fixSignatureCaching', 'fixSignatureCaching.ts']
]
  .map(([name, rel]) => [name, path.join(tsCases, rel)])
  .filter(([, file]) => existingFile([file]));
if (cases.length === 0) {
  skip(`benchmark cases not found under ${tsCases}`);
}

function run(cmd, args, opts = {}) {
  const result = spawnSync(cmd, args, {
    cwd: root,
    encoding: 'utf8',
    maxBuffer: 64 * 1024 * 1024,
    ...opts
  });
  if (result.error) throw result.error;
  return result;
}

const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'skim-bench-'));
const cBin = requireSkimBin();

function bench(bin, file) {
  const samples = [];
  for (let r = 0; r < runs; r++) {
    const start = process.hrtime.bigint();
    for (let i = 0; i < loops; i++) {
      const result = run(bin, [file], { stdio: 'ignore' });
      if (result.status !== 0) process.exit(result.status || 1);
    }
    const end = process.hrtime.bigint();
    samples.push(Number(end - start) / 1e6);
  }
  samples.sort((a, b) => a - b);
  const mean = samples.reduce((a, b) => a + b, 0) / samples.length;
  return { mean, median: samples[Math.floor(samples.length / 2)] };
}

console.log(`bench loops=${loops} runs=${runs}`);
for (const [name, file] of cases) {
  const c = bench(cBin, file);
  const o = bench(oxc, file);
  console.log(
    `${name}: C ${c.mean.toFixed(1)}ms mean (${c.median.toFixed(1)} median), OXC ${o.mean.toFixed(1)}ms mean (${o.median.toFixed(1)} median), ratio ${(o.mean / c.mean).toFixed(2)}x`
  );
}
