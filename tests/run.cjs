const { spawnSync } = require('child_process');
const fs = require('fs');
const os = require('os');
const path = require('path');
const { requireSkimBin, root } = require('./support.cjs');

const here = path.join(root, 'tests');
const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'skim-fixtures-'));
const bin = requireSkimBin();
const meson = fs.readFileSync(path.join(root, 'meson.build'), 'utf8');
const version = meson.match(/version:\s*'([^']+)'/)[1];

function run(cmd, args, opts = {}) {
  const result = spawnSync(cmd, args, { cwd: root, encoding: 'utf8', ...opts });
  if (result.error) throw result.error;
  return result;
}

function assertOk(result, label) {
  if (result.status !== 0) throw new Error(`${label} failed with ${result.status}\nstdout:\n${result.stdout}\nstderr:\n${result.stderr}`);
}

function assertIncludes(text, expected, label) {
  if (!text.includes(expected))
    throw new Error(`${label} missing ${JSON.stringify(expected)}\n\nGenerated JS:\n${text}`);
}

const versionResult = run(bin, ['--version']);
assertOk(versionResult, 'skim --version');
if (versionResult.stdout !== `skim ${version}\n`)
  throw new Error(`--version mismatch\nexpected: skim ${version}\nactual:   ${versionResult.stdout}`);

const cases = [
  ['annotations.ts', 'ant:6 10,none\n'],
  ['class_modifiers.ts', 'ready 3\n'],
  ['enum.ts', '1 2 blue\n'],
  ['enum_refs.ts', '1 3\n'],
  ['export_named.ts', '42\n'],
  ['generics.ts', '4 ok\n'],
  ['import_object_property.ts', '84\n'],
  ['mixed_default_import.ts', '50\n'],
  ['mixed_import.ts', '42\n'],
  ['namespace.ts', '7\n'],
  ['namespace_exports.ts', '11 hi\n'],
  ['non_null_optional.ts', 'yes 5\n'],
  ['object_property_modifier.ts', 'a.ts:true false 1 true\n'],
  ['parameter_property.ts', '9\n'],
  ['parameter_property_super.ts', '12\n'],
  ['import_equals.cjs.ts', 'function\n']
];

for (const support of fs.readdirSync(path.join(here, 'fixtures')).filter(name => name.endsWith('.mjs') || name.endsWith('.cjs'))) {
  fs.copyFileSync(path.join(here, 'fixtures', support), path.join(tmp, support));
}

{
  const source = path.join(here, 'fixtures', 'decorated_async.ts');
  const strip = run(bin, [source]);
  assertOk(strip, 'strip decorated_async.ts');
  assertIncludes(strip.stdout, '@dec\n  async run(', 'decorated_async.ts');
}

{
  const source = path.join(here, 'fixtures', 'import_object_property.ts');
  const strip = run(bin, [source]);
  assertOk(strip, 'strip import_object_property.ts');
  assertIncludes(strip.stdout, 'unusedValue', 'import_object_property.ts');
}

{
  const source = path.join(tmp, 'semicolonless_import_later_line_comment.ts');
  const js = path.join(tmp, 'semicolonless_import_later_line_comment.mjs');
  fs.writeFileSync(source, [
    'import { readFileSync } from "node:fs"',
    'route(',
    '  "/api/auth/*",',
    '  // test "*"',
    '  readFileSync,',
    ')',
    ''
  ].join('\n'));
  const strip = run(bin, [source]);
  assertOk(strip, 'strip semicolonless_import_later_line_comment.ts');
  const importCount = (strip.stdout.match(/import \{ readFileSync \}/g) || []).length;
  if (importCount !== 1)
    throw new Error(`semicolonless_import_later_line_comment.ts emitted ${importCount} imports\n\nGenerated JS:\n${strip.stdout}`);
  assertIncludes(strip.stdout, '// test "*"\n  readFileSync', 'semicolonless_import_later_line_comment.ts');
  fs.writeFileSync(js, strip.stdout);
  assertOk(run(process.execPath, ['--check', js]), 'check semicolonless_import_later_line_comment.ts');
}

for (const [fixture, expected] of cases) {
  const source = path.join(here, 'fixtures', fixture);
  const js = path.join(tmp, fixture.replace(/\.ts$/, fixture.includes('.cjs.') ? '.cjs' : '.mjs'));
  const strip = run(bin, [source]);
  assertOk(strip, `strip ${fixture}`);
  fs.writeFileSync(js, strip.stdout);

  const node = run(process.execPath, [js]);
  assertOk(node, `run ${fixture}`);
  if (node.stdout !== expected)
    throw new Error(
      `${fixture} stdout mismatch\nexpected: ${JSON.stringify(expected)}\nactual:   ${JSON.stringify(node.stdout)}\n\nGenerated JS:\n${strip.stdout}`
    );
}

console.log('skim fixtures passed');
