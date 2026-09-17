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

const MAX_BODY_BYTES = 4096;
const ALLOWED_TOP_LEVEL_KEYS = new Set(['device_token', 'session']);
const ALLOWED_SESSION_KEYS = new Set(FIELD_KEYS);

function jsonResponse_(payload) {
  return ContentService.createTextOutput(JSON.stringify(payload))
    .setMimeType(ContentService.MimeType.JSON);
}

function safeCell_(value) {
  if (typeof value === 'number') return value;
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
  return value;
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

function sheetValue_(key, value) {
  if ((key === 'enter_time' || key === 'exit_time') && value !== '') {
    return new Date(value);
  }
  if (key === 'duration_sec') return value / 86400;
  return safeCell_(value);
}

function ensureSheet_(spreadsheetId) {
  const sheet = SpreadsheetApp.openById(spreadsheetId).getSheets()[0];
  if (sheet.getLastRow() === 0) {
    sheet.appendRow(SHEET_HEADERS);
    return sheet;
  }

  const actualHeaders = sheet.getRange(1, 1, 1, SHEET_HEADERS.length).getValues()[0];
  const isLegacyEnglishSchema = actualHeaders.every((value, index) => value === FIELD_KEYS[index]);
  const isPreviousChineseSchema = actualHeaders.every((value, index) =>
    value === (index === 5 ? '停留秒數' : SHEET_HEADERS[index]));

  if ((isLegacyEnglishSchema || isPreviousChineseSchema) && sheet.getLastColumn() === FIELD_KEYS.length) {
    sheet.getRange(1, 1, 1, SHEET_HEADERS.length).setValues([SHEET_HEADERS]);
    return sheet;
  }

  if (sheet.getLastColumn() !== SHEET_HEADERS.length ||
      actualHeaders.some((value, index) => value !== SHEET_HEADERS[index])) {
    throw new Error('invalid_sheet_schema');
  }
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
          return jsonResponse_({ok: true, duplicate: true});
        }
      }
      sheet.appendRow(FIELD_KEYS.map(key => sheetValue_(key, row[key])));
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

function isoSeconds_(date) {
  return date.toISOString().replace(/\.\d{3}Z$/, 'Z');
}

// Manual Apps Script smoke test. Running this function writes one TEST row
// to the configured spreadsheet using the same doPost() path as the device.
function testWriteSample_() {
  const props = PropertiesService.getScriptProperties();
  const deviceToken = props.getProperty('DEVICE_TOKEN');
  if (!deviceToken) throw new Error('DEVICE_TOKEN is not configured');

  const durationSec = 81;
  const exitTime = new Date();
  const enterTime = new Date(exitTime.getTime() - durationSec * 1000);
  const response = doPost({
    postData: {
      contents: JSON.stringify({
        device_token: deviceToken,
        session: {
          session_id: 'test-' + Utilities.getUuid(),
          chip_id: 'ABC123',
          cat_id: 'TEST',
          enter_time: isoSeconds_(enterTime),
          exit_time: isoSeconds_(exitTime),
          duration_sec: durationSec,
          min_distance_mm: 46,
          avg_distance_mm: 108.9,
          sample_count: 1350,
        },
      }),
    },
  });
  console.log(response.getContent());
}
