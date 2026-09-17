'use strict';

const assert = require('node:assert/strict');
const { readFileSync } = require('node:fs');
const { join } = require('node:path');
const vm = require('node:vm');
const { test } = require('node:test');

const source = readFileSync(join(__dirname, '..', 'Code.gs'), 'utf8');
const FIELDS = [
  'session_id', 'chip_id', 'cat_id', 'enter_time', 'exit_time',
  'duration_sec', 'min_distance_mm', 'avg_distance_mm', 'sample_count'
];
const HEADERS = [
  '紀錄編號', '晶片編號', '貓咪', '進入時間', '離開時間',
  '停留時間', '最短距離（mm）', '平均距離（mm）', '取樣次數'
];
const OLD_HEADERS = HEADERS.map((value, index) => index === 5 ? '停留秒數' : value);

function fixture(overrides = {}) {
  return {
    session_id: 'session-001',
    chip_id: '123456789',
    cat_id: '測試貓',
    enter_time: '2026-09-16T14:37:57Z',
    exit_time: '2026-09-16T14:41:58Z',
    duration_sec: 241,
    min_distance_mm: 69,
    avg_distance_mm: 237.6,
    sample_count: 2435,
    ...overrides,
  };
}

function createHarness(initialRows = []) {
  const rows = initialRows.map(row => [...row]);
  const properties = new Map([
    ['SPREADSHEET_ID', 'sheet-id'],
    ['DEVICE_TOKEN', 'device-token'],
  ]);
  let locked = false;
  let timeZoneWriteCount = 0;
  let numberFormatWriteCount = 0;

  const sheet = {
    getLastRow: () => rows.length,
    getLastColumn: () => rows.length ? Math.max(...rows.map(row => row.length)) : 0,
    appendRow(values) {
      rows.push([...values]);
      return sheet;
    },
    getRange(row, col, rowCount = 1, colCount = 1) {
      const zeroRow = row - 1;
      const zeroCol = col - 1;
      return {
        getValues() {
          return Array.from({ length: rowCount }, (_, r) =>
            Array.from({ length: colCount }, (_, c) => rows[zeroRow + r]?.[zeroCol + c] ?? ''));
        },
        setValues(values) {
          for (let r = 0; r < rowCount; r++) {
            if (!rows[zeroRow + r]) rows[zeroRow + r] = [];
            for (let c = 0; c < colCount; c++) rows[zeroRow + r][zeroCol + c] = values[r][c];
          }
          return this;
        },
        setNumberFormat() {
          numberFormatWriteCount++;
          return this;
        },
        createTextFinder(text) {
          return {
            matchEntireCell() { return this; },
            findNext() {
              for (let r = zeroRow; r < zeroRow + rowCount; r++) {
                if (String(rows[r]?.[zeroCol] ?? '') === text) return { getRow: () => r + 1 };
              }
              return null;
            },
          };
        },
      };
    },
  };

  const spreadsheet = {
    getSheets: () => [sheet],
    getSpreadsheetTimeZone: () => 'Asia/Taipei',
    setSpreadsheetTimeZone() { timeZoneWriteCount++; },
  };

  const context = vm.createContext({
    Date,
    Number,
    String,
    Set,
    JSON,
    Math,
    console: { error() {} },
    ContentService: {
      MimeType: { JSON: 'application/json' },
      createTextOutput(text) {
        return { text, setMimeType() { return this; } };
      },
    },
    PropertiesService: {
      getScriptProperties: () => ({
        getProperty: key => properties.get(key) ?? null,
      }),
    },
    Utilities: {
      newBlob: text => ({ getBytes: () => Buffer.from(text, 'utf8') }),
    },
    SpreadsheetApp: {
      openById(id) {
        assert.equal(id, 'sheet-id');
        return spreadsheet;
      },
    },
    LockService: {
      getScriptLock: () => ({
        tryLock() {
          if (locked) return false;
          locked = true;
          return true;
        },
        releaseLock() { locked = false; },
      }),
    },
  });

  vm.runInContext(source, context, { filename: 'Code.gs' });

  return {
    rows,
    get timeZoneWriteCount() { return timeZoneWriteCount; },
    get numberFormatWriteCount() { return numberFormatWriteCount; },
    post(session = fixture()) {
      const response = context.doPost({ postData: { contents: JSON.stringify({
        device_token: 'device-token',
        session,
      }) } });
      return JSON.parse(response.text);
    },
  };
}

test('stores accepted source values without presentation conversion', () => {
  const h = createHarness();
  const session = fixture();

  assert.deepEqual(h.post(session), { ok: true });
  assert.deepEqual(h.rows[0], HEADERS);
  assert.equal(h.rows[1][3], session.enter_time);
  assert.equal(h.rows[1][4], session.exit_time);
  assert.equal(h.rows[1][5], 241);
  assert.equal(typeof h.rows[1][3], 'string');
  assert.equal(typeof h.rows[1][5], 'number');
  assert.equal(h.timeZoneWriteCount, 0);
  assert.equal(h.numberFormatWriteCount, 0);
});

test('blank timestamps remain blank strings and duration remains seconds', () => {
  const h = createHarness();
  assert.deepEqual(h.post(fixture({ enter_time: '', exit_time: '', duration_sec: 0 })), { ok: true });
  assert.equal(h.rows[1][3], '');
  assert.equal(h.rows[1][4], '');
  assert.equal(h.rows[1][5], 0);
});

test('renames previous Chinese header only and leaves existing data untouched', () => {
  const existing = ['old-session', '123', '貓', '2026-09-16T14:37:57.000Z',
    '2026-09-16T14:41:58.000Z', 241, 69, 237.6, 2435];
  const h = createHarness([OLD_HEADERS, existing]);

  assert.deepEqual(h.post(fixture({ session_id: 'session-new' })), { ok: true });
  assert.deepEqual(h.rows[0], HEADERS);
  assert.deepEqual(h.rows[1], existing);
  assert.equal(h.rows[1][5], 241);
  assert.equal(typeof h.rows[1][3], 'string');
});

test('renames legacy English header only and leaves existing data untouched', () => {
  const existing = ['old-session', '123', '貓', '2026-09-16T14:37:57.000Z',
    '2026-09-16T14:41:58.000Z', 241, 69, 237.6, 2435];
  const h = createHarness([FIELDS, existing]);

  assert.deepEqual(h.post(fixture({ session_id: 'session-new' })), { ok: true });
  assert.deepEqual(h.rows[0], HEADERS);
  assert.deepEqual(h.rows[1], existing);
});

test('duplicate session is idempotent and does not rewrite data', () => {
  const existing = FIELDS.map(key => fixture()[key]);
  const h = createHarness([HEADERS, existing]);
  const snapshot = h.rows.map(row => [...row]);

  assert.deepEqual(h.post(), { ok: true, duplicate: true });
  assert.deepEqual(h.rows, snapshot);
  assert.equal(h.timeZoneWriteCount, 0);
  assert.equal(h.numberFormatWriteCount, 0);
});

test('duration validation still enforces source seconds', () => {
  const h = createHarness();
  assert.deepEqual(h.post(fixture({ duration_sec: 241.5 })), { ok: false, error: 'invalid_duration_sec' });
  assert.deepEqual(h.post(fixture({ duration_sec: 86401 })), { ok: false, error: 'invalid_duration_sec' });
});

test('timestamp validation keeps API contract and rejects inconsistent duration', () => {
  const h = createHarness();
  assert.deepEqual(h.post(fixture({
    enter_time: '2026-09-16T14:37:57.000Z',
    exit_time: '2026-09-16T14:41:58.000Z',
  })), { ok: false, error: 'invalid_enter_time' });

  assert.deepEqual(h.post(fixture({ duration_sec: 200 })), {
    ok: false,
    error: 'inconsistent_time_duration',
  });
});

test('formula-like text is escaped while numeric source values remain numeric', () => {
  const h = createHarness();
  const session = fixture({ cat_id: '=IMPORTXML("x","y")' });
  assert.deepEqual(h.post(session), { ok: true });
  assert.equal(h.rows[1][2], "'=IMPORTXML(\"x\",\"y\")");
  assert.equal(h.rows[1][5], 241);
});
