# Changelog

Versions are the `FW_VERSION` in `firmware/bme688_nose/bme688_nose.ino`.

## 1.1.0 — 2026-09-14

**Documented why there is no CO₂ reading, after trying properly.**

A BME688 can report a CO₂-equivalent, but only through BSEC's IAQ mode and only
after that algorithm completes an internal run-in. On a node that attempted it:
fourteen scheduled IAQ windows — first at 15 minutes, then at 30 — produced **no
value at all**. BSEC never reached run-in, because each return to scan mode
discards the progress. Reaching it appears to need IAQ running continuously,
which costs the fingerprint entirely.

- The firmware no longer implies CO₂ is available. `/status` carries an explicit
  `co2: null` with a note saying why, so anyone scripting against it is not left
  guessing.
- Documented in the README and USAGE: if you want CO₂ in a hive, fit an SCD41 on
  the same two wires. It measures CO₂ with an NDIR sensor rather than inferring
  it from VOC patterns, and does not compete for the heater.

No change to scanning, the web page, labels or export. Flashing is optional.

## 1.0.0 — 2026-09-11

First release.

- BSEC2 scan mode, ten heater steps per fingerprint, ~11 s per scan
- Burst position recorded with every scan: the first scan after the sensor rests
  reads several times high because the sensing surface recovered during the
  pause, and a model trained without knowing that learns the rest cycle instead
  of the smell
- Self-hosted page: live fingerprint, spectrogram over time, intensity trend,
  automatic clustering of recurring shapes, labels stored in NVS, JSON/CSV export
- Export matches the [Apiary Nose](https://github.com/winreboot/apiary-nose)
  session format
