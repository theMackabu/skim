const { spawnSync } = require('child_process');
const fs = require('fs');
const path = require('path');
const { existingDir, existingFile, resolveOxcEmit, resolveOxcRoot, resolveTypeScriptRoot, root } = require('./support.cjs');

const depsDir = path.resolve(process.env.SKIM_TEST_DEPS_DIR || path.join(root, '.cache', 'test-deps'));
const download = process.env.SKIM_TEST_DOWNLOAD !== '0';
const oxcRef = process.env.SKIM_OXC_REF || '6be3d11f99d6b7dd66bc656d5f05fb39e4a20c4b';
const tsRef = process.env.SKIM_TYPESCRIPT_REF || 'f350b52331494b68c90ab02e2b6d0828d2a22a74';

function log(message = '') {
  console.log(message ? `[skim test] ${message}` : '');
}

function rel(p) {
  return path.relative(root, p) || '.';
}

function section(title) {
  log('');
  log(title);
}

function run(cmd, args, opts = {}) {
  const result = spawnSync(cmd, args, {
    cwd: root,
    encoding: 'utf8',
    stdio: opts.capture ? 'pipe' : 'inherit',
    maxBuffer: 64 * 1024 * 1024,
    ...opts
  });
  if (result.error) throw result.error;
  if (result.status !== 0 && !opts.allowFailure) {
    throw new Error(`${cmd} ${args.join(' ')} failed with ${result.status}`);
  }
  return result;
}

function have(cmd) {
  const result = spawnSync(cmd, ['--version'], { encoding: 'utf8', stdio: 'ignore' });
  return !result.error && result.status === 0;
}

function ensureCommand(cmd) {
  if (!have(cmd)) throw new Error(`${cmd} is required`);
}

function oxcRustToolchain(oxcRoot) {
  const toml = path.join(oxcRoot, 'rust-toolchain.toml');
  if (existingFile([toml])) {
    const text = fs.readFileSync(toml, 'utf8');
    const match = text.match(/^\s*channel\s*=\s*"([^"]+)"/m);
    if (match) return match[1];
  }
  const plain = path.join(oxcRoot, 'rust-toolchain');
  if (existingFile([plain])) return fs.readFileSync(plain, 'utf8').trim();
  return '';
}

function cargoCommandForOxc(oxcRoot) {
  const channel = oxcRustToolchain(oxcRoot);
  if (!channel) return { cmd: 'cargo', args: [] };

  log(`ensuring Rust toolchain ${channel} for OXC helper`);
  ensureCommand('rustup');
  run('rustup', ['toolchain', 'install', '--profile', 'minimal', channel]);
  return { cmd: 'cargo', args: [`+${channel}`] };
}

function ensureCheckout(name, url, ref) {
  const dir = path.join(depsDir, name);
  if (!download && !existingDir([dir])) {
    throw new Error(`${name} checkout missing at ${dir}; unset SKIM_TEST_DOWNLOAD=0 or provide env paths`);
  }

  ensureCommand('git');
  if (!existingDir([dir])) {
    log(`creating shallow ${name} checkout at ${rel(dir)}`);
    fs.mkdirSync(depsDir, { recursive: true });
    fs.mkdirSync(dir, { recursive: true });
    run('git', ['-C', dir, 'init']);
    run('git', ['-C', dir, 'remote', 'add', 'origin', url]);
  }

  log(`fetching ${name}@${ref} with --depth=1`);
  const shallow = run('git', ['-C', dir, 'fetch', '--depth=1', 'origin', ref], { allowFailure: true });
  if (shallow.status !== 0) {
    throw new Error(`failed to fetch ${name}@${ref} with depth=1`);
  }
  run('git', ['-C', dir, 'checkout', '--detach', 'FETCH_HEAD']);
  log(`ready ${name}: ${rel(dir)}`);
  return dir;
}

function hasOxcFixtures(dir) {
  return existingDir([
    path.join(dir, 'tasks/transform_conformance/tests/babel-plugin-transform-typescript/test/fixtures'),
    path.join(dir, 'tasks/transform_conformance/overrides/babel-plugin-transform-typescript/test/fixtures')
  ]);
}

function hasTypeScriptCases(dir) {
  return existingDir([path.join(dir, 'tests/cases/conformance')]);
}

function ensureOxcRoot() {
  if (process.env.OXC_ROOT) {
    if (!hasOxcFixtures(process.env.OXC_ROOT)) {
      throw new Error(`OXC_ROOT does not contain transform fixtures: ${process.env.OXC_ROOT}`);
    }
    log(`using OXC_ROOT=${process.env.OXC_ROOT}`);
    return process.env.OXC_ROOT;
  }

  const existing = resolveOxcRoot();
  if (existing && hasOxcFixtures(existing)) {
    log(`using existing OXC checkout: ${rel(existing)}`);
    return existing;
  }
  return ensureCheckout('oxc', 'https://github.com/oxc-project/oxc.git', oxcRef);
}

function ensureTypeScriptRoot() {
  if (process.env.TS_ROOT) {
    if (!hasTypeScriptCases(process.env.TS_ROOT)) {
      throw new Error(`TS_ROOT does not contain tests/cases/conformance: ${process.env.TS_ROOT}`);
    }
    log(`using TS_ROOT=${process.env.TS_ROOT}`);
    return process.env.TS_ROOT;
  }
  if (process.env.TS_CASES) {
    if (!existingDir([process.env.TS_CASES])) {
      throw new Error(`TS_CASES does not exist: ${process.env.TS_CASES}`);
    }
    log(`using TS_CASES=${process.env.TS_CASES}`);
    return '';
  }

  const existing = resolveTypeScriptRoot();
  if (existing && hasTypeScriptCases(existing)) {
    log(`using existing TypeScript checkout: ${rel(existing)}`);
    return existing;
  }
  return ensureCheckout('TypeScript', 'https://github.com/microsoft/TypeScript.git', tsRef);
}

function tomlString(value) {
  return value.replace(/\\/g, '\\\\').replace(/"/g, '\\"');
}

function ensureOxcEmit(oxcRoot) {
  const existing = resolveOxcEmit(oxcRoot);
  if (existing) {
    log(`using existing oxc_emit: ${rel(existing)}`);
    return existing;
  }
  if (!download) throw new Error('oxc_emit missing; unset SKIM_TEST_DOWNLOAD=0 or set OXC_EMIT');

  ensureCommand('cargo');
  const cargo = cargoCommandForOxc(oxcRoot);
  const dir = path.join(depsDir, 'oxc_emit');
  log(`generating oxc_emit helper at ${rel(dir)}`);
  const srcDir = path.join(dir, 'src');
  fs.mkdirSync(srcDir, { recursive: true });
  fs.writeFileSync(
    path.join(dir, 'Cargo.toml'),
    `[package]
name = "oxc_emit"
version = "0.1.0"
edition = "2024"

[dependencies]
oxc_allocator = { path = "${tomlString(path.join(oxcRoot, 'crates', 'oxc_allocator'))}" }
oxc_codegen = { path = "${tomlString(path.join(oxcRoot, 'crates', 'oxc_codegen'))}" }
oxc_parser = { path = "${tomlString(path.join(oxcRoot, 'crates', 'oxc_parser'))}" }
oxc_semantic = { path = "${tomlString(path.join(oxcRoot, 'crates', 'oxc_semantic'))}" }
oxc_span = { path = "${tomlString(path.join(oxcRoot, 'crates', 'oxc_span'))}" }
oxc_transformer = { path = "${tomlString(path.join(oxcRoot, 'crates', 'oxc_transformer'))}" }
`
  );
  fs.writeFileSync(
    path.join(srcDir, 'main.rs'),
    `use std::{env, fs, path::Path};

use oxc_allocator::Allocator;
use oxc_codegen::{Codegen, CodegenOptions};
use oxc_parser::Parser;
use oxc_semantic::SemanticBuilder;
use oxc_span::SourceType;
use oxc_transformer::{HelperLoaderMode, TransformOptions, Transformer};

fn transform(path: &Path, source: &str) -> Result<String, String> {
    let allocator = Allocator::default();
    let source_type = SourceType::from_path(path).map_err(|e| format!("source type: {e}"))?;
    let ret = Parser::new(&allocator, source, source_type).parse();
    if !ret.errors.is_empty() {
        return Err(format!("parse failed: {:?}", ret.errors));
    }
    let mut program = ret.program;
    let ret = SemanticBuilder::new()
        .with_excess_capacity(2.0)
        .with_enum_eval(true)
        .build(&program);
    if !ret.errors.is_empty() {
        return Err(format!("semantic failed: {:?}", ret.errors));
    }
    let scoping = ret.semantic.into_scoping();
    let mut options = TransformOptions::default();
    options.helper_loader.mode = HelperLoaderMode::External;
    let ret = Transformer::new(&allocator, path, &options).build_with_scoping(scoping, &mut program);
    if !ret.errors.is_empty() {
        return Err(format!("transform failed: {:?}", ret.errors));
    }
    Ok(Codegen::new().with_options(CodegenOptions::default()).build(&program).code)
}

fn main() {
    let path = env::args().nth(1).expect("usage: oxc_emit <file.ts>");
    let source = fs::read_to_string(&path).expect("read source");
    match transform(Path::new(&path), &source) {
        Ok(code) => print!("{code}"),
        Err(err) => {
            eprintln!("{err}");
            std::process::exit(2);
        }
    }
}
`
  );
  log(`building oxc_emit helper with ${cargo.cmd} ${cargo.args.join(' ')}`.trim());
  run(cargo.cmd, [...cargo.args, 'build', '--release', '--manifest-path', path.join(dir, 'Cargo.toml')]);
  const bin = path.join(dir, 'target', 'release', process.platform === 'win32' ? 'oxc_emit.exe' : 'oxc_emit');
  if (!existingFile([bin])) throw new Error(`cargo finished but ${bin} was not created`);
  log(`ready oxc_emit: ${rel(bin)}`);
  return bin;
}

function ensureMesonBuild() {
  section('Building Skim with Meson');
  ensureCommand('meson');
  if (!existingDir([path.join(root, 'build')])) {
    run('meson', ['setup', 'build']);
  } else {
    run('meson', ['setup', 'build', '--reconfigure']);
  }
  run('meson', ['compile', '-C', 'build']);
  run('meson', ['test', '-C', 'build', '--print-errorlogs']);
}

function runNode(script, env, opts = {}) {
  section(`Running ${script}`);
  run(process.execPath, [script], {
    env: { ...process.env, ...env },
    capture: false
  });
}

log(`deps cache: ${rel(depsDir)}`);
ensureMesonBuild();

const skimBin = path.join(root, 'build', process.platform === 'win32' ? 'skim.exe' : 'skim');
const env = { SKIM_BIN: skimBin };

runNode('tests/run.cjs', env);

section('Resolving external test dependencies');
const oxcRoot = ensureOxcRoot();
const tsRoot = ensureTypeScriptRoot();
const oxcEmit = process.env.OXC_EMIT || ensureOxcEmit(oxcRoot);

runNode('tests/run_oxc_suite.cjs', { ...env, OXC_ROOT: oxcRoot });
const tsEnv = {
  ...env,
  OXC_ROOT: oxcRoot,
  OXC_EMIT: oxcEmit,
  SKIM_REQUIRED_TEST: '1',
  TS_SUITE_PROGRESS: process.env.TS_SUITE_PROGRESS || '500'
};
if (tsRoot) tsEnv.TS_ROOT = tsRoot;
runNode('tests/run_ts_suite.cjs', tsEnv);

if (process.env.SKIM_RUN_BENCH === '1') {
  runNode('tests/bench_perf.cjs', tsEnv);
}

console.log('all skim tests passed');
