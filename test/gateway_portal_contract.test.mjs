import assert from 'node:assert/strict';
import fs from 'node:fs';
import test from 'node:test';

test('gateway captive portal exposes Wi-Fi only and no Hub credential field', () => {
  const source = fs.readFileSync('src/gateway_main.cpp', 'utf8');
  assert.doesNotMatch(source, /Hub Pilot API key/);
  assert.doesNotMatch(source, /hub_pilot_key/);
  assert.doesNotMatch(source, /wifiManager\.addParameter/);
  assert.match(source, /HUB_BOOTSTRAP_SECRET/);
});


