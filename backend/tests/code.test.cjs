'use strict';

// Control-flow tests, not an emulator for Apps Script or Sheets rendering.
// Run from the repository root: node --test backend/tests/code.test.cjs
const assert = require('node:assert/strict');
const { readFileSync } = require('node:fs');
const { join } = require('node:path');
const vm = require('node:vm');
const { test } = require('node:test');
const source = readFileSync(process.env.CODE_GS_PATH || join(__dirname, '..', 'Code.gs'), 'utf8');
const DAY = 86400;
const TIME_FORMAT = 'yyyy/MM/dd HH:mm:ss';
const DURATION_FORMAT = '[mm]:ss';
const FIELDS = ['session_id', 'chip_id', 'cat_id', 'enter_time', 'exit_time',
  'duration_sec', 'min_distance_mm', 'avg_distance_mm', 'sample_count'];
const HEADERS = ['紀錄編號', '晶片編號', '貓咪', '進入時間', '離開時間',
  '停留時間', '最短距離（mm）', '平均距離（mm）', '取樣次數'];
const OLD_HEADERS = HEADERS.map((value, i) => i === 5 ? '停留秒數' : value);
const clone = value => value instanceof Date ? new Date(value.getTime()) : value;

function fixture(overrides = {}) {
  return { session_id: 'session-new', chip_id: '123456789', cat_id: '測試貓',
    enter_time: '', exit_time: '', duration_sec: 241,
    min_distance_mm: 69, avg_distance_mm: 237.6, sample_count: 2435, ...overrides };
}
function oldRow(seconds = 241, id = 'session-old') {
  return [id, '123456789', '測試貓', '2026-09-16T14:37:57.000Z',
    '2026-09-16T14:41:58.000Z', seconds, 69, 237.6, 2435];
}

function harness({ durationAsDate = false } = {}) {
  const h = { sheets: [], events: [], failures: [], properties: new Map([
    ['SPREADSHEET_ID', 'test-spreadsheet'], ['DEVICE_TOKEN', 'test-only-token']
  ]), locked: false, busy: false, timeZone: 'Etc/UTC' };
  h.failOnce = match => h.failures.push(match);
  function hit(event) {
    h.events.push(event);
    const index = h.failures.findIndex(match => Object.entries(match).every(([k, v]) => event[k] === v));
    if (index !== -1) {
      h.failures.splice(index, 1);
      throw new Error('injected service failure');
    }
  }
  function op(method, details, action) {
    hit({ method, ...details, when: 'before' });
    const result = action();
    hit({ method, ...details, when: 'after' });
    return result;
  }
  h.addSheet = (id, rows = []) => {
    const grid = new Map();
    const cell = (row, col) => {
      const key = `${row}:${col}`;
      if (!grid.has(key)) grid.set(key, { value: '', formula: '', format: 'General' });
      return grid.get(key);
    };
    function write(row, col, input) {
      const c = cell(row, col);
      c.formula = typeof input === 'string' && input.startsWith('=') ? input : '';
      const duration = /^=(\d+)\/86400$/.exec(c.formula);
      c.value = duration ? Number(duration[1]) / DAY : clone(input);
      if (typeof c.value === 'string' && c.value.startsWith("'")) c.value = c.value.slice(1);
    }
    const sheet = { id, cell,
      getSheetId: () => id,
      getLastRow: () => Math.max(0, ...[...grid].filter(([, c]) => c.value !== '' || c.formula)
        .map(([key]) => Number(key.split(':')[0]))),
      getLastColumn: () => Math.max(0, ...[...grid].filter(([, c]) => c.value !== '' || c.formula)
        .map(([key]) => Number(key.split(':')[1]))),
      getRange(row, col, count = 1, width = 1) {
        assert.ok(row >= 1 && col >= 1 && count >= 1 && width >= 1);
        const info = { sheetId: id, row, col };
        const matrix = fn => Array.from({ length: count }, (_, r) =>
          Array.from({ length: width }, (_, c) => fn(cell(row + r, col + c))));
        const range = {
          getRow: () => row,
          getValues: () => op('getValues', info, () => matrix(c => {
            // Exercise Date-typed duration reads without claiming to emulate Sheets.
            if (durationAsDate && /^\[m+\]:ss$/i.test(c.format) && typeof c.value === 'number') {
              return new Date(Date.UTC(1899, 11, 30) + c.value * DAY * 1000);
            }
            return clone(c.value);
          })),
          getFormulas: () => op('getFormulas', info, () => matrix(c => c.formula)),
          getNumberFormats: () => matrix(c => c.format),
          setValues(values) {
            assert.equal(values.length, count);
            values.forEach(r => assert.equal(r.length, width));
            return op('setValues', info, () => {
              values.forEach((r, ri) => r.forEach((v, ci) => op('cellWrite',
                { sheetId: id, row: row + ri, col: col + ci }, () => write(row + ri, col + ci, v))));
              return range;
            });
          },
          setNumberFormat(format) {
            return op('setNumberFormat', info, () => { matrix(c => { c.format = format; }); return range; });
          },
          createTextFinder(text) {
            return { matchEntireCell() { return this; }, findNext() {
              for (let r = row; r < row + count; r++) {
                if (String(cell(r, col).value) === text) return sheet.getRange(r, col);
              }
              return null;
            } };
          }
        };
        return range;
      },
      appendRow(values) {
        const row = sheet.getLastRow() + 1;
        return op('appendRow', { sheetId: id, row }, () => {
          values.forEach((value, i) => write(row, i + 1, value));
          return sheet;
        });
      }
    };
    rows.forEach((r, ri) => r.forEach((v, ci) => write(ri + 1, ci + 1, v)));
    h.sheets.push(sheet);
    return sheet;
  };
  const props = { getProperty: key => h.properties.get(key) ?? null,
    setProperty: (key, value) => op('setProperty', { key }, () => { h.properties.set(key, value); return props; }) };
  const spreadsheet = { getSheets: () => h.sheets,
    getSpreadsheetTimeZone: () => h.timeZone,
    setSpreadsheetTimeZone: zone => { h.timeZone = zone; } };
  const context = vm.createContext({ Date,
    console: { error() {} },
    ContentService: { MimeType: { JSON: 'application/json' }, createTextOutput: text =>
      ({ text, setMimeType() { return this; } }) },
    PropertiesService: { getScriptProperties: () => props },
    Utilities: { newBlob: text => ({ getBytes: () => Buffer.from(text, 'utf8') }) },
    SpreadsheetApp: { openById: id => { assert.equal(id, 'test-spreadsheet'); return spreadsheet; },
      flush: () => op('flush', {}, () => {}) },
    LockService: { getScriptLock: () => ({ tryLock() {
      if (h.busy) return false;
      assert.equal(h.locked, false); h.locked = true; return true;
    }, releaseLock() { h.locked = false; hit({ method: 'releaseLock', when: 'after' }); } }) }
  });
  vm.runInContext(source, context, { filename: 'Code.gs' });
  h.postRaw = raw => JSON.parse(context.doPost({ postData: { contents: raw } }).text);
  h.post = (session = fixture(), extras = {}) => h.postRaw(JSON.stringify({ device_token: 'test-only-token', session, ...extras }));
  h.ensure = () => context.ensureSheet_('test-spreadsheet');
  h.constant = name => vm.runInContext(name, context);
  h.key = id => `SHEET_FORMAT_V3_test-spreadsheet_${id}`;
  return h;
}

function assertStoredDuration(sheet, row, seconds) {
  assert.equal(sheet.cell(row, 6).value, seconds / DAY);
  assert.equal(sheet.cell(row, 6).format, DURATION_FORMAT);
}

for (const seconds of [0, 1, 59, 60, 241, 3599, 3665, 86400]) {
  test(`new row: ${seconds} seconds, blank offline times, unchanged metrics`, () => {
    const h = harness(); const s = h.addSheet(11);
    assert.deepEqual(h.post(fixture({ duration_sec: seconds })), { ok: true });
    assert.equal(h.timeZone, 'Asia/Taipei');
    assertStoredDuration(s, 2, seconds);
    assert.equal(s.cell(2, 4).value, ''); assert.equal(s.cell(2, 5).value, '');
    assert.equal(s.cell(2, 9).value, 2435); assert.equal(s.cell(2, 8).value, 237.6);
    assert.equal(s.cell(1, 6).value, '停留時間'); assert.equal(h.locked, false);
  });
}

test('UTC timestamps preserve the instant and use Taipei display configuration', () => {
  const h = harness(); const s = h.addSheet(11);
  const enter = '2026-09-16T14:37:57Z'; const exit = '2026-09-16T14:41:58Z';
  assert.deepEqual(h.post(fixture({ enter_time: enter, exit_time: exit })), { ok: true });
  assert.equal(s.cell(2, 4).value.toISOString(), '2026-09-16T14:37:57.000Z');
  assert.equal(s.cell(2, 5).value.toISOString(), '2026-09-16T14:41:58.000Z');
  assert.equal(s.cell(2, 4).format, TIME_FORMAT); assert.equal(s.cell(2, 5).format, TIME_FORMAT);
  // Intl verifies the instant/time-zone pair, not the Google Sheets UI.
  const parts = new Intl.DateTimeFormat('en-GB', { timeZone: h.timeZone, hour: '2-digit',
    minute: '2-digit', second: '2-digit', hourCycle: 'h23' }).format(s.cell(2, 4).value);
  assert.equal(parts, '22:37:57');
});

for (const [name, headers] of [['English', FIELDS], ['Chinese', OLD_HEADERS]]) {
  test(`migrate ${name} schema, including zero and one day, and do not migrate twice`, () => {
    const h = harness(); const seconds = [0, 1, 241, 3665, 86400];
    const s = h.addSheet(11, [headers, ...seconds.map((n, i) => oldRow(n, `session-old-${i}`))]);
    assert.deepEqual(h.post(), { ok: true });
    seconds.forEach((n, i) => {
      assertStoredDuration(s, i + 2, n);
      assert.equal(s.cell(i + 2, 6).formula, `=${n}/86400`);
      assert.ok(s.cell(i + 2, 4).value instanceof Date);
    });
    assert.equal(h.properties.get(h.key(11)), 'done');
    const reads = h.events.filter(e => e.method === 'getFormulas').length;
    assert.deepEqual(h.post(), { ok: true, duplicate: true });
    assert.equal(h.events.filter(e => e.method === 'getFormulas').length, reads);
    assert.equal(s.getLastRow(), 7);
  });
}

const migrationFailures = [
  { method: 'setValues', row: 2, col: 4, when: 'after' },
  { method: 'setValues', row: 2, col: 6, when: 'before' },
  { method: 'setValues', row: 2, col: 6, when: 'after' },
  { method: 'cellWrite', row: 2, col: 6, when: 'after' },
  { method: 'setNumberFormat', row: 2, col: 4, when: 'before' },
  { method: 'setNumberFormat', row: 2, col: 6, when: 'before' },
  { method: 'setNumberFormat', row: 2, col: 6, when: 'after' },
  { method: 'flush', when: 'before' },
  { method: 'setProperty', when: 'before' },
  { method: 'setProperty', when: 'after' },
  { method: 'setValues', row: 1, col: 1, when: 'before' },
  { method: 'setValues', row: 1, col: 1, when: 'after' }
];
for (const failure of migrationFailures) {
  test(`migration retry preserves one day: ${JSON.stringify(failure)}`, () => {
    const h = harness({ durationAsDate: true });
    const first = oldRow(86400); first[3] = first[4] = '';
    const s = h.addSheet(11, [OLD_HEADERS, first, oldRow(1, 'session-old-2')]);
    h.failOnce(failure);
    assert.deepEqual(h.post(), { ok: false, error: 'storage_error' });
    assert.equal(h.failures.length, 0); assert.equal(h.locked, false);
    assert.deepEqual(h.post(), { ok: true });
    assertStoredDuration(s, 2, 86400); assertStoredDuration(s, 3, 1);
    assert.equal(s.cell(2, 6).formula, '=86400/86400');
    assert.equal(s.cell(1, 6).value, '停留時間');
    assert.equal(s.getLastRow(), 4);
  });
}

test('each sheet has its own migration marker when the first tab changes', () => {
  const h = harness(); const a = h.addSheet(11, [OLD_HEADERS, oldRow()]);
  const b = h.addSheet(22, [OLD_HEADERS, oldRow()]);
  assert.deepEqual(h.post(), { ok: true });
  assert.equal(b.cell(2, 6).value, 241);
  h.sheets = [b, a];
  assert.deepEqual(h.post(), { ok: true });
  assertStoredDuration(a, 2, 241); assertStoredDuration(b, 2, 241);
  assert.ok(b.cell(2, 4).value instanceof Date);
  assert.equal(h.properties.get(h.key(11)), 'done'); assert.equal(h.properties.get(h.key(22)), 'done');
});

for (const failure of [
  { method: 'appendRow', row: 2, when: 'after' },
  { method: 'setNumberFormat', row: 2, col: 4, when: 'before' },
  { method: 'setNumberFormat', row: 2, col: 6, when: 'before' },
  { method: 'setNumberFormat', row: 2, col: 6, when: 'after' },
  { method: 'flush', when: 'before' }
]) {
  test(`duplicate retry repairs formatting: ${JSON.stringify(failure)}`, () => {
    const h = harness(); const s = h.addSheet(11); h.ensure();
    h.failOnce(failure);
    assert.deepEqual(h.post(), { ok: false, error: 'storage_error' });
    assert.equal(s.getLastRow(), 2); assert.equal(h.failures.length, 0);
    assert.deepEqual(h.post(), { ok: true, duplicate: true });
    assertStoredDuration(s, 2, 241); assert.equal(s.cell(2, 4).format, TIME_FORMAT);
    assert.equal(s.cell(2, 5).format, TIME_FORMAT); assert.equal(s.getLastRow(), 2);
  });
}

test('duplicate repair cannot report success while formatting still fails', () => {
  const h = harness(); const s = h.addSheet(11); h.ensure();
  const failure = { method: 'setNumberFormat', row: 2, col: 6, when: 'before' };
  h.failOnce(failure); assert.equal(h.post().ok, false);
  h.failOnce(failure); assert.equal(h.post().ok, false);
  assert.deepEqual(h.post(), { ok: true, duplicate: true });
  assertStoredDuration(s, 2, 241); assert.equal(s.getLastRow(), 2);
});

for (const durationAsDate of [false, true]) {
  test(`upgrade completed V2 values without reconverting (Date reads=${durationAsDate})`, () => {
    const h = harness({ durationAsDate }); const row = oldRow(1); row[3] = row[4] = '';
    const s = h.addSheet(11, [HEADERS, row]); s.cell(2, 6).format = '[m]:ss';
    h.properties.set('SHEET_FORMAT_V2_test-spreadsheet', 'done');
    assert.deepEqual(h.post(), { ok: true }); assertStoredDuration(s, 2, 86400);
    assert.equal(s.cell(2, 6).formula, '');
  });
}

test('old global V2 marker does not suppress migration of another legacy sheet', () => {
  const h = harness(); const s = h.addSheet(22, [OLD_HEADERS, oldRow()]);
  h.properties.set('SHEET_FORMAT_V2_test-spreadsheet', 'done');
  assert.deepEqual(h.post(), { ok: true }); assertStoredDuration(s, 2, 241);
});

test('ambiguous partially migrated V2 values fail closed instead of guessing units', () => {
  const h = harness(); const s = h.addSheet(11, [HEADERS, oldRow(1)]);
  assert.deepEqual(h.post(), { ok: false, error: 'storage_error' });
  assert.equal(s.cell(2, 6).value, 1); assert.equal(s.getLastRow(), 2);
  assert.equal(h.properties.has(h.key(11)), false);
});

test('migration preserves timestamp formulas and escaped formula-like text', () => {
  const h = harness(); const s = h.addSheet(11, [OLD_HEADERS, oldRow(), oldRow(60, 'session-old-2')]);
  s.cell(2, 5).formula = '=D2+241/86400'; s.cell(2, 5).value = new Date('2026-09-16T14:41:58Z');
  s.cell(3, 5).value = '=not-a-formula';
  assert.deepEqual(h.post(), { ok: true });
  assert.equal(s.cell(2, 5).formula, '=D2+241/86400');
  assert.equal(s.cell(3, 5).formula, ''); assert.equal(s.cell(3, 5).value, '=not-a-formula');
});

test('migration leaves blank cells blank', () => {
  const h = harness(); const row = oldRow(); row[3] = row[4] = row[5] = '';
  const s = h.addSheet(11, [OLD_HEADERS, row]);
  assert.deepEqual(h.post(), { ok: true });
  for (const col of [4, 5, 6]) assert.equal(s.cell(2, col).value, '');
});

test('flush precedes migration completion and successful lock release', () => {
  const h = harness(); h.addSheet(11, [OLD_HEADERS, oldRow()]); assert.equal(h.post().ok, true);
  const methods = h.events.filter(e => e.when === 'after').map(e => e.method);
  assert.equal(methods[methods.indexOf('setProperty') - 1], 'flush');
  assert.deepEqual(methods.slice(-2), ['flush', 'releaseLock']);
});

test('formula injection defense and timestamp validation remain unchanged', () => {
  const h = harness(); const s = h.addSheet(11);
  assert.equal(h.post(fixture({ cat_id: '=1+1' })).ok, true);
  assert.equal(s.cell(2, 3).formula, ''); assert.equal(s.cell(2, 3).value, '=1+1');
  for (const overrides of [
    { enter_time: '2026-09-16T14:37:57Z' },
    { enter_time: '2026-09-16T14:37:57.000Z', exit_time: '2026-09-16T14:41:58Z' },
    { enter_time: '2026-02-30T00:00:00Z', exit_time: '2026-03-01T00:00:00Z' },
    { duration_sec: -1 }, { duration_sec: 1.5 }, { duration_sec: 86401 },
    { enter_time: '2026-09-16T14:37:57Z', exit_time: '2026-09-16T14:41:58Z', duration_sec: 10 }
  ]) assert.equal(h.post(fixture({ session_id: 'session-invalid', ...overrides })).ok, false);
  assert.equal(s.getLastRow(), 2);
});

test('request, authentication, schema, and lock guards still reject invalid writes', () => {
  const h = harness(); const s = h.addSheet(11);
  assert.deepEqual(h.postRaw('{'), { ok: false, error: 'invalid_json' });
  assert.equal(h.postRaw('x'.repeat(4097)).error, 'invalid_request');
  assert.equal(h.post(fixture(), { device_token: 'wrong' }).error, 'unauthorized');
  assert.equal(h.post(fixture(), { unexpected: true }).error, 'unexpected_field');
  assert.equal(h.post(fixture({ unexpected: true })).error, 'unexpected_session_field');
  h.busy = true; assert.equal(h.post().error, 'busy'); h.busy = false;
  assert.equal(s.getLastRow(), 0);
  s.appendRow([...HEADERS, 'extra']);
  assert.equal(h.post().error, 'invalid_sheet_schema'); assert.equal(h.locked, false);
});

test('duration format requires zero-padded elapsed minutes, not clock minutes', () => {
  assert.equal(harness().constant('SHEET_DURATION_FORMAT'), '[mm]:ss');
});
