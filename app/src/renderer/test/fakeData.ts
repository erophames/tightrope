import { faker } from '@faker-js/faker';

const FIXTURE_SEED = 20260405;

function hashKey(input: string): number {
  let hash = 2166136261;
  for (let index = 0; index < input.length; index += 1) {
    hash ^= input.charCodeAt(index);
    hash = Math.imul(hash, 16777619);
  }
  return hash >>> 0;
}

function sanitizeToken(value: string): string {
  return value
    .toLowerCase()
    .replace(/[^a-z0-9]+/g, '-')
    .replace(/^-+|-+$/g, '')
    .slice(0, 24);
}

export function fakeEmail(key: string): string {
  faker.seed(FIXTURE_SEED ^ hashKey(key));
  const first = sanitizeToken(faker.person.firstName()) || 'user';
  const last = sanitizeToken(faker.person.lastName()) || 'test';
  const suffix = sanitizeToken(key) || 'fixture';
  return `${first}.${last}.${suffix}@test.local`;
}
