import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { test } from 'node:test';

import { handlePublic } from '../src/api.js';
import { READERS, readReport } from '../src/reports.js';
import { makeEnv, post } from './harness.js';

// tools/test.ps1 runs the launcher's test first, which leaves the request its client built from a synthetic log, and
// points TSR_CPP_REQUEST at it.
const requestFile = process.env.TSR_CPP_REQUEST;
const skip = requestFile ? false : 'run through tools/test.ps1, which sets TSR_CPP_REQUEST';

test('the request the launcher builds is accepted and stored as previewed', { skip }, async () => {
  const text = readFileSync(requestFile, 'utf8');
  const sent = JSON.parse(text);
  assert.deepEqual(Object.keys(sent).sort(), Object.keys(READERS).sort());
  const { report, code, detail } = readReport(sent);
  assert.equal(code, undefined, `${code} ${detail}`);

  const env = makeEnv();
  const response = await handlePublic(post('/v1/reports', null, { raw: text }), {}, env.desk);
  assert.equal(response.status, 201);
  const { ticket } = await response.json();
  const stored = env.service.get(ticket);
  for (const [field, value] of Object.entries(report)) assert.equal(stored[field], value, field);
  assert.ok(!text.includes('ana.silva') && !text.includes('hunter2') && !text.includes('203.0.113.42'));
});
