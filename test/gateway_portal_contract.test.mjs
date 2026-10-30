import assert from 'node:assert/strict';
import fs from 'node:fs';
import test from 'node:test';

test('gateway captive portal uses manual dashboard routing without bootstrap credentials', () => {
  const source = fs.readFileSync('src/gateway_main.cpp', 'utf8');
  assert.doesNotMatch(source, /Hub Pilot API key/);
  assert.doesNotMatch(source, /hub_pilot_key/);
  assert.doesNotMatch(source, /wifiManager\.addParameter/);
  assert.doesNotMatch(source, /HUB_BOOTSTRAP_SECRET/);
  assert.match(source, /X-Hub-Mode", "manual/);
});
