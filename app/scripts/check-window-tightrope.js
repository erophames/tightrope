#!/usr/bin/env node

const { spawnSync } = require('node:child_process');

const result = spawnSync('rg', ['-n', 'window\\.tightrope', 'src/renderer'], {
  cwd: process.cwd(),
  encoding: 'utf8',
});

if (result.status === 1) {
  process.exit(0);
}

if (result.status !== 0) {
  process.stderr.write(result.stderr || 'Failed to scan for window.tightrope usage\n');
  process.exit(result.status || 1);
}

const allowPath = 'src/renderer/services/tightrope.ts';
const lines = result.stdout.split('\n').map((line) => line.trim()).filter(Boolean);
const violations = lines.filter((line) => {
  const path = line.split(':', 1)[0];
  if (path === allowPath) {
    return false;
  }
  if (path.endsWith('.test.ts') || path.endsWith('.test.tsx')) {
    return false;
  }
  return true;
});

if (violations.length === 0) {
  process.exit(0);
}

process.stderr.write('Direct window.tightrope access is only allowed in src/renderer/services/tightrope.ts and tests.\n');
for (const entry of violations) {
  process.stderr.write(`- ${entry}\n`);
}
process.exit(1);
