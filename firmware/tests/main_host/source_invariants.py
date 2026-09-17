"""Structural guards supplement, but do not replace, firmware/on-device tests."""
from pathlib import Path
import re

root = Path(__file__).resolve().parents[2] / "main"
main = (root / "main.ino").read_text(encoding="utf-8")
web = (root / "management_runtime.h").read_text(encoding="utf-8")
power = (root / "power_runtime.h").read_text(encoding="utf-8")
for token in ("WiFi.softAP(", "WIFI_AP", "NetMode::Ap"):
    assert token not in main + web + power, token
finish = main.split("void finishSession() {", 1)[1].split("void processSensor()", 1)[0]
for token in ("connectSta(", "uploadPending(", "syncClock(", "HTTPClient", "saveDiagnostics("):
    assert token not in finish, token
assert 'authorization.startsWith("Digest ")' in web
assert "if (!requireMutation())" in web
assert web.index("if (!requireMutation())", web.index("void uploadFirmwareChunk")) < web.index("Update.begin(")
assert "ESP_APP_DESC_MAGIC_WORD" in web and "ESP_CHIP_ID_ESP32S3" in web
assert "Update.end(true)" not in main + web
assert "if (otaRequestOpen) { failOta(400); return; }" in web
assert '"Content-Disposition"' in web and '"diagnostics-current.jsonl"' in web
assert "expectedId != id" in web
assert "CONFIG_PM_ENABLE && CONFIG_FREERTOS_USE_TICKLESS_IDLE" in power
assert "if (automaticPm.load()" in power
assert "sdk_pm_or_tickless_unavailable" in power
assert not re.search(r"\b192\.168\.\d+\.\d+\b", main + web + power)
print("source invariants: PASS (structural checks, not runtime integration)")
