#!/usr/bin/env node
'use strict';

const fs = require('node:fs');
const path = require('node:path');
const { spawnSync } = require('node:child_process');

const appDir = path.resolve(__dirname, '..');
const repoRoot = path.resolve(appDir, '..');

const forceRebuild = process.argv.includes('--force');
const dryRun = process.argv.includes('--dry-run');

const nativeModuleCandidates = [
  path.join(repoRoot, 'build', 'Release', 'tightrope-core.node'),
  path.join(repoRoot, 'build', 'tightrope-core.node'),
  path.join(appDir, 'build', 'Release', 'tightrope-core.node'),
  path.join(appDir, 'build', 'tightrope-core.node'),
];

function walkFiles(rootDir, filter, out = []) {
  if (!fs.existsSync(rootDir)) {
    return out;
  }

  const entries = fs.readdirSync(rootDir, { withFileTypes: true });
  for (const entry of entries) {
    const fullPath = path.join(rootDir, entry.name);
    if (entry.isDirectory()) {
      if (
        entry.name === '.git' ||
        entry.name === 'node_modules' ||
        entry.name === 'build' ||
        entry.name === 'build-debug' ||
        entry.name === 'dist'
      ) {
        continue;
      }
      walkFiles(fullPath, filter, out);
      continue;
    }

    if (filter(fullPath)) {
      out.push(fullPath);
    }
  }

  return out;
}

function newestMtimeMs(paths) {
  let newest = 0;
  for (const candidate of paths) {
    if (!candidate || !fs.existsSync(candidate)) {
      continue;
    }
    const mtime = fs.statSync(candidate).mtimeMs;
    if (mtime > newest) {
      newest = mtime;
    }
  }
  return newest;
}

function existingModulePaths() {
  return nativeModuleCandidates.filter((candidate) => fs.existsSync(candidate));
}

function resolveTriplet() {
  const archMap = {
    x64: 'x64',
    arm64: 'arm64',
  };

  const arch = archMap[process.arch];
  if (!arch) {
    throw new Error(`Unsupported CPU architecture: ${process.arch}`);
  }

  if (process.platform === 'darwin') {
    return {
      triplet: `${arch}-osx-static`,
      overlayTriplets: '../triplets',
    };
  }

  if (process.platform === 'linux') {
    return {
      triplet: `${arch}-linux-static`,
      overlayTriplets: '../triplets',
    };
  }

  if (process.platform === 'win32') {
    return {
      triplet: `${arch}-windows-static-md`,
      overlayTriplets: null,
    };
  }

  throw new Error(`Unsupported platform: ${process.platform}`);
}

function readElectronVersion() {
  const electronPkgPath = path.join(appDir, 'node_modules', 'electron', 'package.json');
  if (!fs.existsSync(electronPkgPath)) {
    throw new Error('Electron is not installed. Run `npm --prefix app install` first.');
  }

  const pkg = JSON.parse(fs.readFileSync(electronPkgPath, 'utf8'));
  if (!pkg.version) {
    throw new Error('Unable to resolve Electron version from app/node_modules/electron/package.json.');
  }

  return {
    version: pkg.version,
    packagePath: electronPkgPath,
  };
}

function buildSpawnEnv() {
  const env = {};
  for (const [key, value] of Object.entries(process.env)) {
    if (!key || key.startsWith('=')) {
      continue;
    }
    if (typeof value === 'undefined') {
      continue;
    }
    env[key] = value;
  }
  return env;
}

function runBuild() {
  const { version: electronVersion } = readElectronVersion();
  const { triplet, overlayTriplets } = resolveTriplet();
  const cmakeJsCli = path.join(appDir, 'node_modules', 'cmake-js', 'bin', 'cmake-js');

  if (!fs.existsSync(cmakeJsCli)) {
    throw new Error('cmake-js is not installed. Run `npm --prefix app install` first.');
  }

  const toolchainFileFromEnv = process.env.CMAKE_TOOLCHAIN_FILE;
  const defaultToolchainPath = path.join(repoRoot, 'vcpkg', 'scripts', 'buildsystems', 'vcpkg.cmake');
  const toolchainFile =
    toolchainFileFromEnv ||
    (fs.existsSync(defaultToolchainPath) ? '../vcpkg/scripts/buildsystems/vcpkg.cmake' : null);

  const cmakeArgs = [
    'rebuild',
    '-d',
    '..',
    '-r',
    'electron',
    '-v',
    electronVersion,
    `--CDVCPKG_TARGET_TRIPLET=${process.env.VCPKG_TARGET_TRIPLET || triplet}`,
  ];

  if (overlayTriplets && fs.existsSync(path.join(repoRoot, 'triplets'))) {
    cmakeArgs.push(`--CDVCPKG_OVERLAY_TRIPLETS=${overlayTriplets}`);
  }

  if (toolchainFile) {
    cmakeArgs.push(`--CDCMAKE_TOOLCHAIN_FILE=${toolchainFile}`);
  }

  if (process.platform === 'win32' && !process.env.CMAKE_MSVC_RUNTIME_LIBRARY) {
    cmakeArgs.push('--CDCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL');
  }

  const command = process.execPath;
  const commandArgs = [cmakeJsCli, ...cmakeArgs];
  console.log(`[native] Building tightrope-core.node (node ${path.relative(appDir, cmakeJsCli)} ${cmakeArgs.join(' ')})`);
  const result = spawnSync(command, commandArgs, {
    cwd: appDir,
    stdio: 'inherit',
    env: buildSpawnEnv(),
  });

  if (typeof result.status === 'number' && result.status !== 0) {
    process.exit(result.status);
  }

  if (result.error) {
    throw result.error;
  }

  const builtOutputs = existingModulePaths();
  if (builtOutputs.length === 0) {
    throw new Error('Build succeeded but tightrope-core.node was not found in expected output paths.');
  }

  console.log(`[native] Output: ${builtOutputs[0]}`);
}

function shouldRebuild() {
  const outputs = existingModulePaths();
  if (outputs.length === 0) {
    return { rebuild: true, reason: 'native module is missing' };
  }

  const { packagePath: electronPkgPath } = readElectronVersion();
  const sourceFiles = [
    path.join(repoRoot, 'CMakeLists.txt'),
    path.join(repoRoot, 'CMakePresets.json'),
    path.join(repoRoot, 'vcpkg.json'),
    path.join(appDir, 'package.json'),
    path.join(appDir, 'package-lock.json'),
    electronPkgPath,
    ...walkFiles(path.join(repoRoot, 'native'), (fullPath) =>
      /\.(c|cc|cpp|cxx|h|hh|hpp|ipp|inl|cmake|txt)$/i.test(fullPath)
    ),
    ...walkFiles(path.join(repoRoot, 'triplets'), (fullPath) => /\.cmake$/i.test(fullPath)),
  ];

  const newestSourceMtime = newestMtimeMs(sourceFiles);
  const newestOutputMtime = newestMtimeMs(outputs);

  if (newestOutputMtime >= newestSourceMtime) {
    return { rebuild: false, reason: 'native module is up to date' };
  }

  return { rebuild: true, reason: 'native module is older than native source/build inputs' };
}

function main() {
  try {
    if (forceRebuild) {
      if (dryRun) {
        console.log('[native] --dry-run: force rebuild requested.');
        return;
      }
      runBuild();
      return;
    }

    const { rebuild, reason } = shouldRebuild();
    if (!rebuild) {
      console.log(`[native] ${reason}.`);
      return;
    }

    console.log(`[native] ${reason}; rebuilding.`);
    if (dryRun) {
      return;
    }

    runBuild();
  } catch (error) {
    const message = error instanceof Error ? error.message : String(error);
    console.error(`[native] ${message}`);
    process.exit(1);
  }
}

main();
