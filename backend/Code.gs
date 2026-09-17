const FIELD_KEYS = [
  'session_id',
  'chip_id',
  'cat_id',
  'enter_time',
  'exit_time',
  'duration_sec',
  'min_distance_mm',
  'avg_distance_mm',
  'sample_count',
];

const SHEET_HEADERS = [
  '紀錄編號',
  '晶片編號',
  '貓咪',
  '進入時間',
  '離開時間',
  '停留時間',
  '最短距離（mm）',
  '平均距離（mm）',
  '取樣次數',
];

const PREVIOUS_SHEET_HEADERS = [
  '紀錄編號',
  '晶片編號',
  '貓咪',
  '進入時間',
  '離開時間',
  '停留秒數',
  '最短距離（mm）',
  '平均距離（mm）',
  '取樣次數',
];

const MAX_BODY_BYTES = 4096;
const SHEET_TIME_ZONE = 'Asia/Taipei';
const SHEET_DATETIME_FORMAT = 'yyyy/MM/dd HH:mm:ss';
const SHEET_DURATION_FORMAT = '[mm]:ss';
const SHEET_MIGRATION_PREFIX = 'SHEET_FORMAT_V3_';
const ALLOWED_TOP_LEVEL_KEYS = new Set(['device_token', 'session']);
const ALLOWED_SESSION_KEYS = new Set(FIELD_KEYS);

function jsonResponse_(payload) {
  return ContentService.createTextOutput(JSON.stringify(payload))
    .setMimeType(ContentService.MimeType.JSON);
}

function safeCell_(value) {
  if (typeof value === 'number' || value instanceof Date) return value;
  const text = String(value == null ? '' : value);
  return /^[=+\-@]/.test(text) ? "'" + text : text;
}

function finiteNumber_(value, name, min, max) {
  if (typeof value !== 'number' || !Number.isFinite(value) || value < min || value > max) {
    throw new Error('invalid_' + name);
  }
  return value;
}

function integer_(value, name, min, max) {
  const number = finiteNumber_(value, name, min, max);
  if (!Number.isInteger(number)) throw new Error('invalid_' + name);
  return number;
}

function isoTime_(value, name) {
  if (value === '') return '';
  if (typeof value !== 'string' || !/^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}Z$/.test(value)) {
    throw new Error('invalid_' + name);
  }
  const ms = Date.parse(value);
  const now = Date.now();
  if (!Number.isFinite(ms) || new Date(ms).toISOString().replace('.000Z', 'Z') !== value ||
      ms > now + 10 * 60000) {
    throw new Error('invalid_' + name);
  }
  return new Date(ms).toISOString();
}

function validate_(session) {
  if (!session || typeof session !== 'object' || Array.isArray(session)) throw new Error('invalid_session');
  Object.keys(session).forEach(key => {
    if (!ALLOWED_SESSION_KEYS.has(key)) throw new Error('unexpected_session_field');
  });
  if (typeof session.session_id !== 'string' || !/^[A-Za-z0-9_-]{8,80}$/.test(session.session_id)) {
    throw new Error('invalid_session_id');
  }
  if (typeof session.chip_id !== 'string' ||
      (session.chip_id !== 'unknown' && !/^[A-Fa-f0-9]{1,16}$/.test(session.chip_id))) {
    throw new Error('invalid_chip_id');
  }
  if (typeof session.cat_id !== 'string' || session.cat_id.length < 1 || session.cat_id.length > 40) {
    throw new Error('invalid_cat_id');
  }
  const enterTime = isoTime_(session.enter_time, 'enter_time');
  const exitTime = isoTime_(session.exit_time, 'exit_time');
  if ((enterTime === '') !== (exitTime === '')) throw new Error('invalid_time_pair');
  const durationSec = integer_(session.duration_sec, 'duration_sec', 0, 86400);
  if (enterTime !== '') {
    const deltaSec = (Date.parse(exitTime) - Date.parse(enterTime)) / 1000;
    if (deltaSec < 0 || Math.abs(durationSec - deltaSec) > 2) {
      throw new Error('inconsistent_time_duration');
    }
  }
  return {
    session_id: session.session_id,
    chip_id: session.chip_id,
    cat_id: session.cat_id,
    enter_time: enterTime,
    exit_time: exitTime,
    duration_sec: durationSec,
    min_distance_mm: integer_(session.min_distance_mm, 'min_distance_mm', 0, 8190),
    avg_distance_mm: finiteNumber_(session.avg_distance_mm, 'avg_distance_mm', 0, 8190),
    sample_count: integer_(session.sample_count, 'sample_count', 1, 1000000),
  };
}

function isStoredIsoTime_(value) {
  return typeof value === 'string' &&
    /^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}(?:\.\d{3})?Z$/.test(value) &&
    Number.isFinite(Date.parse(value));
}

function formatTimeColumns_(sheet, startRow, rowCount) {
  sheet.getRange(startRow, 4, rowCount, 2).setNumberFormat(SHEET_DATETIME_FORMAT);
  sheet.getRange(startRow, 6, rowCount, 1).setNumberFormat(SHEET_DURATION_FORMAT);
}

function durationMigrationValues_(range, fromSeconds) {
  const values = range.getValues();
  const formulas = range.getFormulas();
  const formats = range.getNumberFormats();
  let changed = false;
  const migrated = values.map((row, index) => {
    const value = row[0];
    const formula = formulas[index][0];
    if (formula && /^=\d+\/86400$/.test(formula)) return [formula];
    if (value === '' && formula === '') return [''];
    if (!fromSeconds) {
      // Earlier, fully migrated revisions used [m]:ss. Never infer units from magnitude.
      if (!/^\[m+\]:ss$/i.test(formats[index][0])) {
        throw new Error('ambiguous_stored_duration');
      }
      return [formula || value];
    }
    // A self-contained formula retains the original seconds and the unit conversion
    // in the SAME cell write. Retries preserve it, even after a partial batch write.
    if (formula) throw new Error('ambiguous_stored_duration');
    integer_(value, 'stored_duration', 0, 86400);
    changed = true;
    return ['=' + value + '/86400'];
  });
  return changed ? migrated : null;
}

function migrateSheetValues_(sheet, spreadsheetId, durationsAreSeconds) {
  const props = PropertiesService.getScriptProperties();
  const migrationKey = SHEET_MIGRATION_PREFIX + spreadsheetId + '_' + sheet.getSheetId();
  if (props.getProperty(migrationKey) === 'done') return;

  const lastRow = sheet.getLastRow();
  if (lastRow > 1) {
    const durationRange = sheet.getRange(2, 6, lastRow - 1, 1);
    const durationValues = durationMigrationValues_(durationRange, durationsAreSeconds);
    const timeRange = sheet.getRange(2, 4, lastRow - 1, 2);
    const timeValues = timeRange.getValues();
    const timeFormulas = timeRange.getFormulas();
    let timeChanged = false;
    const migratedTimes = timeValues.map((row, rowIndex) => row.map((value, colIndex) => {
      if (timeFormulas[rowIndex][colIndex]) return timeFormulas[rowIndex][colIndex];
      if (isStoredIsoTime_(value)) {
        timeChanged = true;
        return new Date(value);
      }
      return safeCell_(value);
    }));

    if (timeChanged) timeRange.setValues(migratedTimes);
    if (durationValues) durationRange.setValues(durationValues);
    formatTimeColumns_(sheet, 2, lastRow - 1);
  }

  // Commit pending spreadsheet writes before marking this specific sheet complete.
  SpreadsheetApp.flush();
  props.setProperty(migrationKey, 'done');
}

function sheetValue_(key, value) {
  if ((key === 'enter_time' || key === 'exit_time') && value !== '') {
    return new Date(value);
  }
  if (key === 'duration_sec') return value / 86400;
  return safeCell_(value);
}

function ensureSheet_(spreadsheetId) {
  const spreadsheet = SpreadsheetApp.openById(spreadsheetId);
  if (spreadsheet.getSpreadsheetTimeZone() !== SHEET_TIME_ZONE) {
    spreadsheet.setSpreadsheetTimeZone(SHEET_TIME_ZONE);
  }

  const sheet = spreadsheet.getSheets()[0];
  let migrateHeaders = false;
  if (sheet.getLastRow() === 0) {
    sheet.appendRow(SHEET_HEADERS);
  } else {
    const actualHeaders = sheet.getRange(1, 1, 1, SHEET_HEADERS.length).getValues()[0];
    const isLegacySchema = actualHeaders.every((value, index) => value === FIELD_KEYS[index]);
    const isPreviousSchema = actualHeaders.every((value, index) => value === PREVIOUS_SHEET_HEADERS[index]);
    if ((isLegacySchema || isPreviousSchema) && sheet.getLastColumn() === FIELD_KEYS.length) {
      migrateHeaders = true;
    } else if (sheet.getLastColumn() !== SHEET_HEADERS.length ||
        actualHeaders.some((value, index) => value !== SHEET_HEADERS[index])) {
      throw new Error('invalid_sheet_schema');
    }
  }

  migrateSheetValues_(sheet, spreadsheetId, migrateHeaders);
  // Keep the old unit-bearing header until all values and formats are durable.
  if (migrateHeaders) sheet.getRange(1, 1, 1, SHEET_HEADERS.length).setValues([SHEET_HEADERS]);
  return sheet;
}

function doPost(e) {
  try {
    const raw = e && e.postData && e.postData.contents;
    if (typeof raw !== 'string' || raw.length === 0 || raw.length > MAX_BODY_BYTES ||
        Utilities.newBlob(raw).getBytes().length > MAX_BODY_BYTES) {
      return jsonResponse_({ok: false, error: 'invalid_request'});
    }
    let body;
    try {
      body = JSON.parse(raw);
    } catch (_) {
      return jsonResponse_({ok: false, error: 'invalid_json'});
    }

    if (!body || typeof body !== 'object' || Array.isArray(body)) {
      return jsonResponse_({ok: false, error: 'invalid_request'});
    }
    if (Object.keys(body).some(key => !ALLOWED_TOP_LEVEL_KEYS.has(key))) {
      return jsonResponse_({ok: false, error: 'unexpected_field'});
    }

    const props = PropertiesService.getScriptProperties();
    const expectedToken = props.getProperty('DEVICE_TOKEN');
    if (!expectedToken || typeof body.device_token !== 'string' || body.device_token !== expectedToken) {
      return jsonResponse_({ok: false, error: 'unauthorized'});
    }
    const row = validate_(body.session);
    const spreadsheetId = props.getProperty('SPREADSHEET_ID');
    if (!spreadsheetId) throw new Error('server_not_configured');

    const lock = LockService.getScriptLock();
    if (!lock.tryLock(10000)) return jsonResponse_({ok: false, error: 'busy'});
    try {
      const sheet = ensureSheet_(spreadsheetId);
      const lastRow = sheet.getLastRow();
      if (lastRow > 1) {
        const duplicate = sheet.getRange(2, 1, lastRow - 1, 1)
          .createTextFinder(row.session_id)
          .matchEntireCell(true)
          .findNext();
        if (duplicate) {
          formatTimeColumns_(sheet, duplicate.getRow(), 1);
          SpreadsheetApp.flush();
          return jsonResponse_({ok: true, duplicate: true});
        }
      }

      sheet.appendRow(FIELD_KEYS.map(key => sheetValue_(key, row[key])));
      formatTimeColumns_(sheet, sheet.getLastRow(), 1);
      SpreadsheetApp.flush();
    } finally {
      lock.releaseLock();
    }
    return jsonResponse_({ok: true});
  } catch (error) {
    const message = String(error && error.message || error);
    const known = /^(invalid_[a-z_]+|unexpected_[a-z_]+|inconsistent_[a-z_]+|unauthorized|busy|server_not_configured)$/.exec(message);
    const errorCode = known ? known[1] : 'storage_error';
    console.error('doPost failed: ' + errorCode);
    return jsonResponse_({ok: false, error: errorCode});
  }
}
