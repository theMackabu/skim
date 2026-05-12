const { spawnSync } = require('child_process');
const fs = require('fs');
const os = require('os');
const path = require('path');
const { requireSkimBin, resolveOxcRoot, root, skip } = require('./support.cjs');

const here = path.join(root, 'tests');
const oxcRoot = resolveOxcRoot();
if (!oxcRoot) {
  skip('OXC fixture checkout not found. Set OXC_ROOT, clone OXC next to this repo, or run `node tests/run_all.cjs` to download deps.');
}
const fixtureRoots = [
  path.join(oxcRoot, 'tasks/transform_conformance/tests/babel-plugin-transform-typescript/test/fixtures'),
  path.join(oxcRoot, 'tasks/transform_conformance/overrides/babel-plugin-transform-typescript/test/fixtures')
].filter(dir => fs.existsSync(dir));
if (fixtureRoots.length === 0) {
  skip(`OXC TypeScript fixture directories not found under ${oxcRoot}. Run \`node tests/run_all.cjs\` to fetch the pinned checkout.`);
}

const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'skim-oxc-suite-'));
const bin = requireSkimBin();

function run(cmd, args, opts = {}) {
  const result = spawnSync(cmd, args, { cwd: root, encoding: 'utf8', ...opts });
  if (result.error) throw result.error;
  return result;
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

function fixtureDirs(base) {
  const dirs = new Set();
  for (const file of walk(base)) {
    if (/^input\.[cm]?[jt]sx?$/.test(path.basename(file))) dirs.add(path.dirname(file));
  }
  return [...dirs].sort();
}

function findFile(dir, prefix) {
  return fs.readdirSync(dir).find(name => name.startsWith(prefix + '.'));
}

function optionsForFixture(dir, base) {
  const flags = [];
  const labels = [];
  for (let cur = dir; cur.startsWith(base); cur = path.dirname(cur)) {
    const options = path.join(cur, 'options.json');
    if (fs.existsSync(options)) {
      const text = fs.readFileSync(options, 'utf8').trim();
      if (!text) {
        // no-op
      } else {
        const compact = text.replace(/\s+/g, '');
        const defaultPlugin = '{"plugins":[["transform-typescript"]]}';
        if (compact !== defaultPlugin) {
          const json = JSON.parse(text);
          const raw = JSON.stringify(json);
          if (raw.includes('"onlyRemoveTypeImports":true')) {
            flags.push('--only-remove-type-imports');
            labels.push('onlyRemoveTypeImports');
          }
          if (raw.includes('"optimizeEnums":true')) {
            flags.push('--optimize-enums');
            labels.push('optimizeEnums');
          }
          if (raw.includes('"optimizeConstEnums":true')) {
            flags.push('--optimize-const-enums');
            labels.push('optimizeConstEnums');
          }
          if (raw.includes('"removeClassFieldsWithoutInitializer":true')) {
            flags.push('--remove-class-fields-without-initializer');
            labels.push('removeClassFieldsWithoutInitializer');
          }
          if (raw.includes('"allowDeclareFields":false')) {
            flags.push('--allow-declare-fields-false');
            labels.push('allowDeclareFields:false');
          }
          if (raw.includes('transform-class-properties')) {
            flags.push('--transform-class-properties');
            labels.push('transform-class-properties');
          }
          if (raw.includes('"setPublicClassFields":true')) {
            flags.push('--set-public-class-fields');
            labels.push('setPublicClassFields');
          }
        }
      }
    }
    if (cur === base) break;
  }
  return { flags: [...new Set(flags)], labels: [...new Set(labels)] };
}

function normalize(code) {
  return code
    .replace(/\/\* @__PURE__ \*\//g, '')
    .replace(/\/\*[\s\S]*?\*\//g, '')
    .replace(/\/\/[^\n\r]*/g, '')
    .replace(/@\w+\s+/g, '')
    .replace(/\b(?:static\s+)?accessor\s*\[[^\]]*\]\s*=\s*[^;]+;/g, '')
    .replace(/["']/g, '"')
    .replace(/\s+/g, ' ')
    .replace(/\s*([{}()[\],;:=+\-*/<>|&?.])\s*/g, '$1')
    .replace(/,([}\]])/g, '$1')
    .replace(/\(("(?:[^"\\]|\\.)*"|-?(?:Infinity|NaN|\d+(?:\.\d+)?))\)/g, '$1')
    .replace(/\((-?\d+(?:\.\d+)?)\)\./g, '$1.')
    .replace(/;$/g, '')
    .trim();
}

let total = 0;
let skipped = 0;
const skipReasons = new Map();
const optionBuckets = new Map();
let syntaxPass = 0;
let textPass = 0;
const failures = [];

function skipFixture(rel, reason) {
  skipped++;
  const list = skipReasons.get(reason) || [];
  list.push(rel);
  skipReasons.set(reason, list);
}

for (const base of fixtureRoots) {
  for (const dir of fixtureDirs(base)) {
    const inputName = findFile(dir, 'input');
    const outputName = findFile(dir, 'output');
    if (!inputName || !outputName) continue;
    const rel = path.relative(base, dir);
    const input = path.join(dir, inputName);
    const output = path.join(dir, outputName);
    const ext = path.extname(inputName);

    if (ext === '.tsx' || ext === '.jsx') {
      skipFixture(rel, 'tsx/jsx');
      continue;
    }
    const fixtureOptions = optionsForFixture(dir, base);
    const label = fixtureOptions.labels.length ? fixtureOptions.labels.join(', ') : 'default';
    optionBuckets.set(label, (optionBuckets.get(label) || 0) + 1);
    if (
      fixtureOptions.labels.includes('transform-class-properties') ||
      fixtureOptions.labels.includes('setPublicClassFields')
    ) {
      skipFixture(rel, 'class field lowering');
      continue;
    }

    total++;
    const strip = run(bin, [...fixtureOptions.flags, input]);
    if (strip.status !== 0) {
      failures.push({ rel, kind: 'transform', detail: strip.stderr || strip.stdout });
      continue;
    }

    const generated = path.join(tmp, rel.replace(/[\/\\]/g, '__') + '.mjs');
    fs.writeFileSync(generated, strip.stdout);
    const check = run(process.execPath, ['--check', generated]);
    if (check.status !== 0) {
      failures.push({ rel, kind: 'syntax', detail: check.stderr.split('\n').slice(0, 6).join('\n') });
      continue;
    }
    syntaxPass++;

    const expected = fs.readFileSync(output, 'utf8');
    if (normalize(strip.stdout) === normalize(expected)) {
      textPass++;
    } else {
      failures.push({
        rel,
        kind: 'text',
        detail: `expected: ${normalize(expected).slice(0, 220)}\nactual:   ${normalize(strip.stdout).slice(0, 220)}`
      });
    }
  }
}

console.log(`OXC TS fixtures: total=${total} skipped=${skipped} syntax_pass=${syntaxPass} text_pass=${textPass}`);
for (const [label, count] of [...optionBuckets.entries()].sort()) {
  console.log(`option bucket ${label}: ${count}`);
}
if (skipReasons.size) {
  for (const [reason, list] of [...skipReasons.entries()].sort()) {
    console.log(`skipped ${reason}: ${list.length}`);
    if (process.env.OXC_SUITE_VERBOSE_SKIPS === '1') {
      for (const rel of list) console.log(`  - ${rel}`);
    }
  }
}
if (failures.length) {
  console.log(`failures=${failures.length}`);
  for (const f of failures.slice(0, 40)) {
    console.log(`\n[${f.kind}] ${f.rel}\n${f.detail}`);
  }
  process.exit(1);
}
