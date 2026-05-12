const fs = require('fs');
const os = require('os');
const path = require('path');
const root = path.resolve(__dirname, '..');

function existingDir(candidates) {
  for (const candidate of candidates.filter(Boolean)) {
    if (fs.existsSync(candidate) && fs.statSync(candidate).isDirectory()) return candidate;
  }
  return '';
}

function existingFile(candidates) {
  for (const candidate of candidates.filter(Boolean)) {
    if (fs.existsSync(candidate) && fs.statSync(candidate).isFile()) return candidate;
  }
  return '';
}

function findOnPath(name) {
  const dirs = (process.env.PATH || '').split(path.delimiter).filter(Boolean);
  const exts = process.platform === 'win32' ? (process.env.PATHEXT || '.EXE;.CMD;.BAT').split(';') : [''];
  for (const dir of dirs)
    for (const ext of exts) {
      const candidate = path.join(dir, name + ext);
      if (fs.existsSync(candidate) && fs.statSync(candidate).isFile()) return candidate;
    }
  return '';
}

function resolveOxcRoot() {
  return existingDir([
    process.env.OXC_ROOT,
    path.join(root, '.cache', 'test-deps', 'oxc'),
    path.join(root, '..', 'oxc'),
    path.join(root, 'vendor', 'oxc'),
    path.join(os.homedir(), 'Developer', 'oxc'),
    path.join(os.homedir(), 'Developer', 'c_cxx', 'oxc')
  ]);
}

function resolveTypeScriptRoot() {
  return existingDir([
    process.env.TS_ROOT,
    path.join(root, '.cache', 'test-deps', 'TypeScript'),
    path.join(root, '..', 'TypeScript'),
    path.join(root, 'vendor', 'TypeScript'),
    path.join(os.homedir(), 'Developer', 'TypeScript'),
    path.join(os.homedir(), 'Developer', 'c_cxx', 'TypeScript')
  ]);
}

function resolveOxcEmit(oxcRoot = resolveOxcRoot()) {
  return existingFile([
    process.env.OXC_EMIT,
    findOnPath('oxc_emit'),
    path.join(root, '.cache', 'test-deps', 'oxc_emit', 'target', 'release', 'oxc_emit'),
    path.join(root, '.cache', 'test-deps', 'oxc_emit', 'target', 'release', 'oxc_emit.exe'),
    oxcRoot && path.join(oxcRoot, 'target', 'release', 'oxc_emit'),
    oxcRoot && path.join(oxcRoot, 'target', 'release', 'oxc_emit.exe')
  ]);
}

function resolveSkimBin() {
  return existingFile([process.env.SKIM_BIN, path.join(root, 'build', process.platform === 'win32' ? 'skim.exe' : 'skim')]);
}

function resolveNodeBin() {
  const execPath = process.execPath || '';
  return existingFile([
    process.env.NODE_BIN,
    process.env.NODE,
    path.basename(execPath).startsWith('node') ? execPath : '',
    findOnPath('node')
  ]);
}

function requireNodeBin() {
  const bin = resolveNodeBin();
  if (!bin) {
    console.error('error: node binary not found. Set NODE_BIN or put node on PATH.');
    process.exit(1);
  }
  return bin;
}

function requireSkimBin() {
  const bin = resolveSkimBin();
  if (!bin) {
    console.error('error: skim binary not found. Run `meson setup build` and `meson compile -C build`, or set SKIM_BIN.');
    process.exit(1);
  }
  return bin;
}

function skip(message) {
  const text = `skip: ${message}`;
  if (process.env.SKIM_REQUIRED_TEST === '1') {
    console.error(text);
    process.exit(1);
  }
  console.log(text);
  process.exit(0);
}

module.exports = {
  root,
  existingDir,
  existingFile,
  requireNodeBin,
  requireSkimBin,
  resolveNodeBin,
  resolveOxcEmit,
  resolveOxcRoot,
  resolveSkimBin,
  resolveTypeScriptRoot,
  skip
};
