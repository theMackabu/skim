const { spawn } = require('child_process');
const fs = require('fs');
const os = require('os');
const path = require('path');
const { normalizeOfficialTypeScriptOutput: normalize } = require('./normalize.cjs');
const { existingDir, requireNodeBin, requireSkimBin, resolveOxcEmit, resolveOxcRoot, resolveTypeScriptRoot, root, skip } = require('./support.cjs');

const tsRoot = resolveTypeScriptRoot();
const tsCases = process.env.TS_CASES || (tsRoot ? path.join(tsRoot, 'tests/cases/conformance') : '');
const oxcRoot = resolveOxcRoot();
const oxcEmit = resolveOxcEmit(oxcRoot);

if (!existingDir([tsCases]))
  skip(
    'TypeScript conformance cases not found. Set TS_CASES or TS_ROOT, clone TypeScript next to this repo, or run `node tests/run_all.cjs` to download deps.'
  );
if (!oxcEmit)
  skip('oxc_emit not found. Set OXC_EMIT, put oxc_emit on PATH, set OXC_ROOT with a built target/release/oxc_emit, or run `node tests/run_all.cjs`.');

const limit = Number(process.env.TS_SUITE_LIMIT || '0');
const offset = Number(process.env.TS_SUITE_OFFSET || '0');
const filter = process.env.TS_SUITE_FILTER || '';
const progressEvery = Number(process.env.TS_SUITE_PROGRESS || '0');
const jobs = Math.max(1, Number(process.env.TS_SUITE_JOBS || Math.max(1, Math.min(os.cpus().length, 8))));
const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'skim-ts-official-'));
const cBin = requireSkimBin();
const nodeBin = requireNodeBin();

function run(cmd, args, opts = {}) {
  return new Promise((resolve, reject) => {
    const child = spawn(cmd, args, {
      cwd: root,
      stdio: ['ignore', 'pipe', 'pipe'],
      ...opts
    });
    let stdout = '';
    let stderr = '';
    child.stdout.setEncoding('utf8');
    child.stderr.setEncoding('utf8');
    child.stdout.on('data', chunk => {
      stdout += chunk;
      if (stdout.length > 64 * 1024 * 1024) {
        child.kill();
        reject(new Error(`${cmd} stdout exceeded max buffer`));
      }
    });
    child.stderr.on('data', chunk => {
      stderr += chunk;
      if (stderr.length > 64 * 1024 * 1024) {
        child.kill();
        reject(new Error(`${cmd} stderr exceeded max buffer`));
      }
    });
    child.on('error', reject);
    child.on('close', status => resolve({ status, stdout, stderr }));
  });
}

function walk(dir, out = []) {
  if (!fs.existsSync(dir)) return out;
  for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
    const p = path.join(dir, entry.name);
    if (entry.isDirectory()) walk(p, out);
    else out.push(p);
  }
  return out;
}

function shouldSkipSource(text) {
  if (/^\s*\/\/\s*@jsx:/m.test(text)) return 'jsx option';
  if (/^\s*\/\/\s*@allowJs:/m.test(text)) return 'allowJs';
  if (/^\s*\/\/\s*@checkJs:/m.test(text)) return 'checkJs';
  if (/^\s*\/\/\s*@filename:/im.test(text)) return 'multifile harness';
  if (/^\s*\/\/\s*@declaration:/m.test(text)) return 'declaration emit';
  return '';
}

let files = walk(tsCases)
  .filter(file => file.endsWith('.ts') && !file.endsWith('.d.ts') && !file.endsWith('.tsx'))
  .sort();
if (filter) files = files.filter(file => file.includes(filter));
if (offset > 0) files = files.slice(offset);
if (limit > 0) files = files.slice(0, limit);

console.error(`TypeScript suite: cases=${tsCases}`);
console.error(`TypeScript suite: files=${files.length} limit=${limit || 'none'} offset=${offset} filter=${filter || 'none'}`);
console.error(`TypeScript suite: skim=${cBin}`);
console.error(`TypeScript suite: oxc_emit=${oxcEmit}`);
console.error(`TypeScript suite: node=${nodeBin}`);
console.error(`TypeScript suite: jobs=${jobs}`);
if (progressEvery > 0) console.error(`TypeScript suite: progress every ${progressEvery} scanned files`);

let total = 0;
let skipped = 0;
let oxcSkipped = 0;
let cTransformPass = 0;
let textPass = 0;
let syntaxChecked = 0;
let syntaxPass = 0;
const skipReasons = new Map();
const failures = [];
let processed = 0;
let nextFile = 0;

function noteSkip(reason) {
  skipped++;
  skipReasons.set(reason, (skipReasons.get(reason) || 0) + 1);
}

async function processFile(file, index) {
  const rel = path.relative(tsCases, file);
  const source = fs.readFileSync(file, 'utf8');
  const skipReason = shouldSkipSource(source);
  if (skipReason) return { scanned: 1, skipped: skipReason };

  const oxc = await run(oxcEmit, [file]);
  if (oxc.status !== 0) return { scanned: 1, total: 1, oxcSkipped: 1 };

  const c = await run(cBin, [file]);
  if (c.status !== 0) {
    return {
      scanned: 1,
      total: 1,
      failure: { rel, kind: 'c-transform', detail: (c.stderr || c.stdout).slice(0, 800) }
    };
  }

  let syntaxChecked = 0;
  let syntaxPass = 0;
  const oxcJs = path.join(tmp, `oxc-${index}.mjs`);
  const cJs = path.join(tmp, `c-${index}.mjs`);
  fs.writeFileSync(oxcJs, oxc.stdout);
  fs.writeFileSync(cJs, c.stdout);
  const oxcCheck = await run(nodeBin, ['--check', oxcJs]);
  if (oxcCheck.status === 0) {
    syntaxChecked = 1;
    const cCheck = await run(nodeBin, ['--check', cJs]);
    if (cCheck.status === 0) syntaxPass = 1;
    else {
      return {
        scanned: 1,
        total: 1,
        cTransformPass: 1,
        syntaxChecked,
        failure: { rel, kind: 'c-syntax', detail: cCheck.stderr.split('\n').slice(0, 8).join('\n') }
      };
    }
  }

  if (normalize(c.stdout) === normalize(oxc.stdout)) {
    return { scanned: 1, total: 1, cTransformPass: 1, syntaxChecked, syntaxPass, textPass: 1 };
  }

  return {
    scanned: 1,
    total: 1,
    cTransformPass: 1,
    syntaxChecked,
    syntaxPass,
    failure: {
      rel,
      kind: 'text',
      detail: [`oxc: ${normalize(oxc.stdout).slice(0, 500)}`, `c:   ${normalize(c.stdout).slice(0, 500)}`].join('\n')
    }
  };
}

function applyResult(result) {
  processed++;
  if (progressEvery > 0 && (processed === 1 || processed % progressEvery === 0)) {
    process.stderr.write(`progress scanned=${processed}/${files.length} total=${total} failures=${failures.length}\n`);
  }
  if (result.skipped) noteSkip(result.skipped);
  total += result.total || 0;
  oxcSkipped += result.oxcSkipped || 0;
  cTransformPass += result.cTransformPass || 0;
  syntaxChecked += result.syntaxChecked || 0;
  syntaxPass += result.syntaxPass || 0;
  textPass += result.textPass || 0;
  if (result.failure) failures.push(result.failure);
}

async function worker() {
  while (nextFile < files.length) {
    const index = nextFile++;
    const result = await processFile(files[index], index);
    applyResult(result);
  }
}

async function main() {
  await Promise.all(Array.from({ length: Math.min(jobs, files.length) }, () => worker()));
}

main().then(() => {
console.log(`TypeScript official cases: scanned=${files.length} total=${total} skipped=${skipped} oxc_skipped=${oxcSkipped}`);
console.log(`C transform pass=${cTransformPass}`);
console.log(`syntax compared=${syntaxChecked} syntax_pass=${syntaxPass}`);
console.log(`text_pass=${textPass}`);
for (const [reason, count] of [...skipReasons.entries()].sort()) console.log(`skipped ${reason}: ${count}`);
if (failures.length) {
  console.log(`failures=${failures.length}`);
  for (const failure of failures.slice(0, Number(process.env.TS_SUITE_MAX_FAILURES || '40'))) {
    console.log(`\n[${failure.kind}] ${failure.rel}\n${failure.detail}`);
  }
  process.exit(1);
}
}).catch(err => {
  console.error(err && err.stack ? err.stack : String(err));
  process.exit(1);
});
