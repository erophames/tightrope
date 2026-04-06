#!/usr/bin/env node
'use strict';

const fs = require('node:fs');
const path = require('node:path');

const appDir = path.resolve(__dirname, '..');
const repoRoot = path.resolve(appDir, '..');
const outputDir = path.join(appDir, 'native');
const outputPath = path.join(outputDir, 'tightrope-core.node');

function nativeModuleCandidates() {
  return [
    path.join(repoRoot, 'build', 'Release', 'tightrope-core.node'),
    path.join(repoRoot, 'build', 'tightrope-core.node'),
    path.join(appDir, 'build', 'Release', 'tightrope-core.node'),
    path.join(appDir, 'build', 'tightrope-core.node'),
  ];
}

function resolveNativeModulePath() {
  for (const candidate of nativeModuleCandidates()) {
    if (fs.existsSync(candidate)) {
      return candidate;
    }
  }
  return null;
}

function stageNativeModule() {
  const sourcePath = resolveNativeModulePath();
  if (!sourcePath) {
    throw new Error(
      'Could not find a release native module. Run `npm --prefix app run ensure:native:release` first.'
    );
  }

  fs.mkdirSync(outputDir, { recursive: true });
  fs.copyFileSync(sourcePath, outputPath);
  console.log(`[native] staged ${path.relative(appDir, sourcePath)} -> ${path.relative(appDir, outputPath)}`);
}

function main() {
  try {
    stageNativeModule();
  } catch (error) {
    const message = error instanceof Error ? error.message : String(error);
    console.error(`[native] ${message}`);
    process.exit(1);
  }
}

main();
