/*
 * =============================================================================
 *  BME688 Nose — a self-contained smell recorder on an ESP32-S3
 * =============================================================================
 *
 *  One board, one sensor, one web page. No broker, no database, no server.
 *  The ESP32 runs the Bosch BSEC2 library in scan mode, keeps the last few
 *  hundred scans in RAM, and serves a page that draws them.
 *
 *  WHAT A "SCAN" IS
 *  The BME688's gas element sits on a heater that steps through ten
 *  temperatures, roughly 11 s for the set. Different compounds react most
 *  strongly at different temperatures, so a scan yields ten resistances - a
 *  fingerprint - rather than one air-quality number. Shape is the information.
 *
 *  WHAT THE SENSOR CANNOT DO
 *  It does not identify compounds. It has no absolute scale: readings depend on
 *  the individual chip, its burn-in age, the enclosure, temperature and
 *  humidity. Two smells together do not produce the sum of their fingerprints.
 *  Meaning comes only from labelled examples you collect yourself.
 *
 *  THE BURST CYCLE (the thing that will fool you)
 *  BSEC scan mode does not run continuously. It scans a few times, then rests
 *  for about a minute and a half. During the rest the sensing surface recovers,
 *  so the FIRST scan after a pause reads several times higher than the last one
 *  before it. That swing is larger than most smells produce. Every scan here
 *  records its position in the burst, and the page hides position 1 by default.
 *  Nothing is smoothed or discarded in storage - the filter is a view.
 *
 *  WIRING (see WIRING.md for the diagram)
 *    BME688 VCC  -> 3V3            SDA (labelled MOSI on some boards) -> GPIO21
 *    BME688 GND  -> GND            SCL                                -> GPIO13
 *    2.2k pull-ups on SDA and SCL to 3V3, at the sensor end of the cable.
 *    220 uF across VCC/GND at the sensor if the cable is longer than ~30 cm.
 *    MISO and CS stay unconnected (they are for SPI).
 *
 *  LIBRARIES
 *    Arduino IDE -> Library Manager -> "bsec2" (Bosch Sensortec). Accept the
 *    "BME68x Sensor library" dependency. Board: "ESP32S3 Dev Module".
 *
 *  FIRST RUN
 *    Set WIFI_SSID / WIFI_PASS below, flash, open the serial monitor at 115200
 *    and browse to the address it prints. Bosch advise 24-48 h of continuous
 *    running on a new sensor before the gas readings settle.
 *
 *  WHY THERE IS NO CO2 READING HERE
 *  The BME688 can report a CO2-EQUIVALENT, but only through BSEC's IAQ mode,
 *  and only after that algorithm completes an internal run-in. Measured on a
 *  node that tried: fourteen scheduled IAQ windows, first at 15 minutes and
 *  then at 30, produced no value at all - BSEC never reached run-in, because
 *  every return to scan mode discards the progress. Reaching it appears to
 *  require running IAQ CONTINUOUSLY, which costs the fingerprint entirely.
 *
 *  So this firmware does not pretend. It records smell and leaves CO2 alone.
 *  If you want CO2 in a hive, an SCD41 on the same two wires measures it
 *  properly with an NDIR sensor and does not compete for the heater.
 *
 *  (The figure is an inference from VOC patterns in any case, not a
 *  measurement. Losing it costs less than it sounds like.)
 *
 *  v1.1.0
 * =============================================================================
 */

#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include <Preferences.h>
#include <bsec2.h>

// ---------------------------------------------------------------- settings --
#define WIFI_SSID     "YOUR_WIFI"
#define WIFI_PASS     "YOUR_PASSWORD"
#define NODE_NAME     "bme688-nose"     // also the mDNS/host name
#define FW_VERSION    "1.1.0"

#define I2C_SDA       21
#define I2C_SCL       13
#define I2C_HZ        100000UL
#define BME_ADDR_A    0x77              // most breakouts; switch may select 0x76
#define BME_ADDR_B    0x76

#define NOSE_STEPS        10
#define SCAN_RING         400           // scans kept in RAM (~16 KB)
#define BURST_GAP_MS      30000UL       // a longer pause means a new burst began
#define LABEL_MAX         24            // labels kept in NVS
#define TEMP_OFFSET_C     0.0f          // raise if the enclosure reads warm

// Bosch's bundled scan configuration supplies the standard 10-step heater
// profile (HP-354). Its two demo classes are ignored here; we record the raw
// fingerprint, which is what training tools want.
const uint8_t bsecConfig[] = {
  #include "config/FieldAir_HandSanitizer/bsec_selectivity.txt"
};
static const int HEATER_C[NOSE_STEPS] = {320, 100, 100, 100, 200, 200, 200, 320, 320, 320};

// ----------------------------------------------------------------- globals --
Bsec2       envSensor;
WebServer   server(80);
Preferences prefs;

struct Scan {
  uint32_t ms;            // millis() when the scan completed
  uint32_t epoch;         // wall clock, 0 until NTP has synced
  float    g[NOSE_STEPS]; // kOhm per heater step
  float    tempC, rh, hpa;
  uint8_t  pos;           // 1 = first scan after a rest
  uint16_t gapS;          // seconds since the previous scan
};
Scan     g_ring[SCAN_RING];
uint16_t g_head = 0, g_count = 0;
uint32_t g_seq = 0;

float    g_spec[NOSE_STEPS];
uint8_t  g_got = 0;
uint8_t  g_pos = 0;
uint32_t g_lastScanMs = 0;
float    g_tempC = NAN, g_rh = NAN, g_hpa = NAN;
uint8_t  g_addr = 0;
bool     g_ok = false;
uint32_t g_bootMs = 0;

struct Label { uint32_t epoch; char text[40]; };
Label    g_labels[LABEL_MAX];
uint8_t  g_labelCount = 0;

// ------------------------------------------------------------------ labels --
static void labelsLoad() {
  prefs.begin("nose", true);
  String blob = prefs.getString("labels", "");
  prefs.end();
  g_labelCount = 0;
  int i = 0;
  while (i < (int)blob.length() && g_labelCount < LABEL_MAX) {
    int nl = blob.indexOf('\n', i);
    if (nl < 0) nl = blob.length();
    String line = blob.substring(i, nl);
    int sep = line.indexOf('|');
    if (sep > 0) {
      g_labels[g_labelCount].epoch = (uint32_t)line.substring(0, sep).toInt();
      strncpy(g_labels[g_labelCount].text, line.substring(sep + 1).c_str(), 39);
      g_labels[g_labelCount].text[39] = 0;
      g_labelCount++;
    }
    i = nl + 1;
  }
}

static void labelsSave() {
  String blob;
  for (uint8_t i = 0; i < g_labelCount; i++) {
    blob += String(g_labels[i].epoch); blob += '|'; blob += g_labels[i].text; blob += '\n';
  }
  prefs.begin("nose", false);
  prefs.putString("labels", blob);
  prefs.end();
}

// -------------------------------------------------------------------- BSEC --
static void bsecReport(const char* where) {
  if (envSensor.status < BSEC_OK)
    Serial.printf("[bsec] error at %s: %d\n", where, (int)envSensor.status);
  if (envSensor.sensor.status < BME68X_OK)
    Serial.printf("[bsec] bme68x error at %s: %d\n", where, (int)envSensor.sensor.status);
}

void onBsecData(const bme68xData data, const bsecOutputs outputs, Bsec2 bsec) {
  // Capture the raw frame FIRST. In scan mode BSEC emits its virtual outputs
  // only at the end of a scan, so steps 0..8 arrive with nOutputs == 0 - and
  // returning early on that loses nine of the ten resistances.
  int idx = data.gas_index;
  if (idx >= 0 && idx < NOSE_STEPS) {
    g_spec[idx] = data.gas_resistance / 1000.0f;
    g_got++;
    if (idx == NOSE_STEPS - 1) {
      uint32_t now = millis();
      uint32_t gap = g_lastScanMs ? (now - g_lastScanMs) : 0;
      if (!g_lastScanMs || gap > BURST_GAP_MS) g_pos = 1; else if (g_pos < 255) g_pos++;
      g_lastScanMs = now;

      Scan& s = g_ring[g_head];
      s.ms = now;
      time_t tt = time(nullptr);
      s.epoch = (tt > 1600000000) ? (uint32_t)tt : 0;
      memcpy(s.g, g_spec, sizeof(g_spec));
      s.tempC = g_tempC; s.rh = g_rh; s.hpa = g_hpa;
      s.pos = g_pos;
      s.gapS = (uint16_t)min<uint32_t>(gap / 1000UL, 65535);
      g_head = (g_head + 1) % SCAN_RING;
      if (g_count < SCAN_RING) g_count++;
      g_seq++;

      if (g_seq <= 3 || (g_seq % 10) == 0) {
        float sum = 0; int n = 0;
        for (int k = 0; k < NOSE_STEPS; k++) { if (!isnan(s.g[k])) { sum += s.g[k]; n++; } }
        Serial.printf("[nose] scan %lu (%u/10 steps, burst pos %u, +%us) mean %.0f kOhm\n",
                      (unsigned long)g_seq, g_got, s.pos, s.gapS, n ? sum / n : 0.0f);
      }
      g_got = 0;
    }
  }

  if (!outputs.nOutputs) return;
  for (uint8_t i = 0; i < outputs.nOutputs; i++) {
    const bsecData& o = outputs.output[i];
    switch (o.sensor_id) {
      case BSEC_OUTPUT_RAW_TEMPERATURE: g_tempC = o.signal; break;
      case BSEC_OUTPUT_RAW_HUMIDITY:    g_rh    = o.signal; break;
      case BSEC_OUTPUT_RAW_PRESSURE:    g_hpa   = o.signal; break;
      default: break;
    }
  }
}

static bool i2cPing(uint8_t a) {
  Wire.beginTransmission(a);
  return Wire.endTransmission() == 0;
}

static bool sensorBegin() {
  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(I2C_HZ);
  Wire.setTimeOut(100);

  g_addr = i2cPing(BME_ADDR_A) ? BME_ADDR_A : (i2cPing(BME_ADDR_B) ? BME_ADDR_B : 0);
  if (!g_addr) {
    Serial.println("[bme688] nothing answers at 0x77 or 0x76.");
    Serial.println("         Check VCC and GND at the sensor's own pins first, then SDA/SCL,");
    Serial.println("         then the module's address switch if it has one.");
    Serial.print("[i2c] devices on the bus:");
    uint8_t found = 0;
    for (uint8_t a = 1; a < 0x78; a++) if (i2cPing(a)) { Serial.printf(" 0x%02X", a); found++; }
    Serial.println(found ? "" : " none");
    return false;
  }

  if (!envSensor.begin(g_addr, Wire)) { bsecReport("begin"); return false; }
  if (!envSensor.setConfig(bsecConfig)) bsecReport("setConfig");
  envSensor.setTemperatureOffset(TEMP_OFFSET_C);

  bsecSensor list[] = {
    BSEC_OUTPUT_RAW_TEMPERATURE, BSEC_OUTPUT_RAW_PRESSURE,
    BSEC_OUTPUT_RAW_HUMIDITY, BSEC_OUTPUT_RAW_GAS, BSEC_OUTPUT_RAW_GAS_INDEX
  };
  if (!envSensor.updateSubscription(list, sizeof(list) / sizeof(list[0]), BSEC_SAMPLE_RATE_SCAN)) {
    bsecReport("updateSubscription"); return false;
  }
  envSensor.attachCallback(onBsecData);
  Serial.printf("[bme688] found at 0x%02X, BSEC %d.%d.%d.%d, scan mode (10 heater steps, ~11 s)\n",
                g_addr, envSensor.version.major, envSensor.version.minor,
                envSensor.version.major_bugfix, envSensor.version.minor_bugfix);
  for (int k = 0; k < NOSE_STEPS; k++) g_spec[k] = NAN;
  return true;
}

// ------------------------------------------------------------------ web API --
static void sendJson(const String& body) {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", body);
}

// GET /api/scans?minutes=N&settled=1
static void handleScans() {
  uint32_t mins = server.hasArg("minutes") ? (uint32_t)server.arg("minutes").toInt() : 60;
  if (mins < 1) mins = 1;
  if (mins > 24 * 60) mins = 24 * 60;
  bool settled = server.arg("settled") == "1";
  uint32_t cutoff = (uint32_t)mins * 60000UL;

  String j;
  j.reserve(12000);
  j = "{\"ok\":true,\"fw\":\"" FW_VERSION "\",\"node\":\"" NODE_NAME "\",\"seq\":" + String(g_seq) +
      ",\"sensor_ok\":" + String(g_ok ? "true" : "false") +
      ",\"steps_c\":[";
  for (int k = 0; k < NOSE_STEPS; k++) { if (k) j += ','; j += String(HEATER_C[k]); }
  j += "],\"scans\":[";
  bool first = true;
  uint32_t now = millis();
  for (uint16_t i = 0; i < g_count; i++) {
    const Scan& s = g_ring[(g_head + SCAN_RING - g_count + i) % SCAN_RING];
    if (now - s.ms > cutoff) continue;
    if (settled && s.pos == 1) continue;
    if (!first) j += ',';
    first = false;
    j += "{\"ago_s\":" + String((now - s.ms) / 1000UL) +
         ",\"epoch\":" + String(s.epoch) +
         ",\"pos\":" + String(s.pos) +
         ",\"gap_s\":" + String(s.gapS) + ",\"g\":[";
    for (int k = 0; k < NOSE_STEPS; k++) {
      if (k) j += ',';
      j += isnan(s.g[k]) ? String("null") : String(s.g[k], 1);
    }
    j += "],\"t\":" + (isnan(s.tempC) ? String("null") : String(s.tempC, 1)) +
         ",\"rh\":" + (isnan(s.rh) ? String("null") : String(s.rh, 1)) +
         ",\"hpa\":" + (isnan(s.hpa) ? String("null") : String(s.hpa, 1)) + "}";
  }
  j += "],\"labels\":[";
  for (uint8_t i = 0; i < g_labelCount; i++) {
    if (i) j += ',';
    j += "{\"epoch\":" + String(g_labels[i].epoch) + ",\"text\":\"" + String(g_labels[i].text) + "\"}";
  }
  j += "]}";
  sendJson(j);
}

// POST /api/label?text=...   DELETE via /api/label?del=<epoch>
static void handleLabel() {
  if (server.hasArg("del")) {
    uint32_t e = (uint32_t)server.arg("del").toInt();
    uint8_t w = 0;
    for (uint8_t i = 0; i < g_labelCount; i++) if (g_labels[i].epoch != e) g_labels[w++] = g_labels[i];
    g_labelCount = w;
    labelsSave();
    sendJson("{\"ok\":true}");
    return;
  }
  String text = server.arg("text");
  text.trim();
  if (!text.length()) { server.send(400, "application/json", "{\"ok\":false,\"error\":\"text required\"}"); return; }
  if (g_labelCount >= LABEL_MAX) {                 // drop the oldest
    for (uint8_t i = 1; i < g_labelCount; i++) g_labels[i - 1] = g_labels[i];
    g_labelCount--;
  }
  time_t tt = time(nullptr);
  g_labels[g_labelCount].epoch = (tt > 1600000000) ? (uint32_t)tt : (millis() / 1000UL);
  strncpy(g_labels[g_labelCount].text, text.c_str(), 39);
  g_labels[g_labelCount].text[39] = 0;
  g_labelCount++;
  labelsSave();
  sendJson("{\"ok\":true}");
}

// GET /api/export?minutes=N&label=..&kind=..&format=json|csv&settled=1
static void handleExport() {
  uint32_t mins = server.hasArg("minutes") ? (uint32_t)server.arg("minutes").toInt() : 20;
  if (mins < 1) mins = 1;
  if (mins > 24 * 60) mins = 24 * 60;
  String label = server.arg("label"); label.trim();
  String kind = server.hasArg("kind") ? server.arg("kind") : "reference";
  String notes = server.arg("notes");
  bool csv = server.arg("format") == "csv";
  bool settled = server.arg("settled") == "1";
  if (!label.length()) { server.send(400, "text/plain", "a label is required\n"); return; }

  uint32_t cutoff = (uint32_t)mins * 60000UL, now = millis();
  String out;
  out.reserve(16000);

  if (csv) {
    out = "t_epoch,burst_pos,";
    for (int k = 0; k < NOSE_STEPS; k++) out += "g" + String(k) + "_kohm,";
    for (int k = 0; k < NOSE_STEPS; k++) out += "step" + String(k) + "_c,";
    out += "temp_c,rh_pct,hpa,label,kind\n";
    for (uint16_t i = 0; i < g_count; i++) {
      const Scan& s = g_ring[(g_head + SCAN_RING - g_count + i) % SCAN_RING];
      if (now - s.ms > cutoff) continue;
      if (settled && s.pos == 1) continue;
      out += String(s.epoch) + "," + String(s.pos) + ",";
      for (int k = 0; k < NOSE_STEPS; k++) out += (isnan(s.g[k]) ? String("") : String(s.g[k], 1)) + ",";
      for (int k = 0; k < NOSE_STEPS; k++) out += String(HEATER_C[k]) + ",";
      out += (isnan(s.tempC) ? String("") : String(s.tempC, 1)) + "," +
             (isnan(s.rh) ? String("") : String(s.rh, 1)) + "," +
             (isnan(s.hpa) ? String("") : String(s.hpa, 1)) + "," + label + "," + kind + "\n";
    }
    server.sendHeader("Content-Disposition", "attachment; filename=\"" + label + ".csv\"");
    server.send(200, "text/csv", out);
    return;
  }

  out = "{\n  \"schema_version\": 1,\n  \"apiary_id\": \"" NODE_NAME "\",\n";
  out += "  \"label\": \"" + label + "\",\n  \"label_kind\": \"" + kind + "\",\n";
  out += "  \"confidence\": \"certain\",\n  \"notes\": \"" + notes + "\",\n";
  out += "  \"firmware\": \"bme688_nose " FW_VERSION "\",\n";
  out += "  \"sensor\": {\"part\": \"BME688\", \"unit_id\": \"" NODE_NAME "\", \"heater_profile\": \"HP-354\",\n";
  out += "    \"steps_c\": [";
  for (int k = 0; k < NOSE_STEPS; k++) { if (k) out += ','; out += String(HEATER_C[k]); }
  out += "]},\n  \"scans\": [\n";
  bool first = true;
  for (uint16_t i = 0; i < g_count; i++) {
    const Scan& s = g_ring[(g_head + SCAN_RING - g_count + i) % SCAN_RING];
    if (now - s.ms > cutoff) continue;
    if (settled && s.pos == 1) continue;
    if (!first) out += ",\n";
    first = false;
    out += "    {\"epoch\": " + String(s.epoch) + ", \"burst_pos\": " + String(s.pos) + ", \"g_kohm\": [";
    for (int k = 0; k < NOSE_STEPS; k++) { if (k) out += ','; out += isnan(s.g[k]) ? String("null") : String(s.g[k], 1); }
    out += "], \"temp_c\": " + (isnan(s.tempC) ? String("null") : String(s.tempC, 1)) +
           ", \"rh_pct\": " + (isnan(s.rh) ? String("null") : String(s.rh, 1)) +
           ", \"hpa\": " + (isnan(s.hpa) ? String("null") : String(s.hpa, 1)) + "}";
  }
  out += "\n  ]\n}\n";
  server.sendHeader("Content-Disposition", "attachment; filename=\"" + label + ".json\"");
  server.send(200, "application/json", out);
}

// ------------------------------------------------------------------- page ---
static const char PAGE[] PROGMEM = R"HTML(<!doctype html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>BME688 Nose</title>
<style>
:root{--bg:#14110c;--pan:#1b1712;--edge:rgba(232,185,35,.22);--honey:#e8b923;--cream:#e9dcc3;--mut:#9a8a6a}
*{box-sizing:border-box}
body{background:var(--bg);color:var(--cream);font:14px/1.5 system-ui,sans-serif;margin:0;padding:18px;max-width:860px;margin:auto}
h1{color:var(--honey);font-size:22px;margin:6px 0 2px}
h2{color:var(--honey);font-size:12px;letter-spacing:.08em;text-transform:uppercase;margin:22px 0 8px}
.sub{color:var(--mut);font-size:12px}
.card{background:var(--pan);border:1px solid var(--edge);border-radius:12px;padding:12px 14px;margin-top:10px}
button,select,input{background:transparent;color:var(--honey);border:1px solid var(--edge);border-radius:999px;
  padding:6px 13px;margin:0 6px 6px 0;cursor:pointer;font-size:12px;font-family:inherit}
button.on{background:var(--honey);color:var(--bg);font-weight:600}
input,select{border-radius:8px;color:var(--cream);cursor:text}
.bars{display:flex;gap:4px;align-items:flex-end;height:80px}
.bars i{flex:1;background:var(--honey);border-radius:3px 3px 0 0;min-height:2px;transition:height .3s}
.lab{display:flex;justify-content:space-between;font-size:10px;color:var(--mut);margin-top:4px}
canvas{width:100%;border:1px solid var(--edge);border-radius:10px;background:rgba(0,0,0,.3);display:block}
table{width:100%;border-collapse:collapse;font-size:12px}
td,th{padding:5px 6px;border-bottom:1px solid rgba(255,255,255,.07);text-align:left}
th{color:var(--mut);font-weight:500}
.pat{display:grid;grid-template-columns:repeat(auto-fit,minmax(150px,1fr));gap:8px}
.pat>div{border:1px solid var(--edge);border-radius:10px;padding:8px 10px}
.pat .bars{height:34px}
.x{color:var(--mut);cursor:pointer;padding:0 6px}
.note{color:var(--mut);font-size:12px;margin-top:6px}
a{color:var(--honey)}
</style></head><body>
<h1>BME688 Nose</h1>
<div class="sub" id="hdr">connecting…</div>

<div class="card">
  <div id="win"></div>
  <h2 style="margin-top:6px">Fingerprint now</h2>
  <div class="bars" id="nowBars"></div>
  <div class="lab" id="stepLab"></div>
  <div class="note" id="nowMsg"></div>
</div>

<div class="card">
  <h2 style="margin-top:0">Fingerprint over time</h2>
  <canvas id="spec" width="800" height="200"></canvas>
  <div class="lab"><span id="specFrom"></span><span>rows = heater steps · brighter = more gas at that temperature · ▲ = labels</span><span>now</span></div>
</div>

<div class="card">
  <h2 style="margin-top:0">Smell intensity</h2>
  <canvas id="line" width="800" height="170"></canvas>
  <div class="lab"><span>0 % = cleanest air in this window</span><span>100 % = strongest</span></div>
</div>

<div class="card">
  <h2 style="margin-top:0">Recurring patterns</h2>
  <div class="pat" id="pats"></div>
  <div class="note">Fingerprint shapes grouped automatically, intensity removed. Same colour = the nose met the same kind of mixture.</div>
</div>

<div class="card">
  <h2 style="margin-top:0">Label this moment</h2>
  <input id="lblIn" placeholder="what is happening now? e.g. coffee, clear room, nectar flow" maxlength="39" style="width:60%">
  <button onclick="addLabel()">Add</button>
  <div id="lblList"></div>
</div>

<div class="card">
  <h2 style="margin-top:0">Export</h2>
  <input id="xpLabel" placeholder="label" maxlength="39" style="width:36%">
  <select id="xpKind"><option value="reference">reference</option><option value="colony">colony</option><option value="ambient">ambient</option></select>
  <input id="xpMins" type="number" min="1" max="1440" value="20" style="width:80px"> <span class="sub">minutes back</span>
  <button onclick="dl('json')">Download JSON</button>
  <button onclick="dl('csv')">CSV</button>
  <div class="note">Files follow the Apiary Nose session format, so they can be contributed to the shared dataset or loaded straight into pandas.</div>
</div>

<div class="sub" style="margin:18px 0 30px">
  Burst position is recorded with every scan. The first scan after the sensor rests reads
  several times high because the surface recovered — that is the sensor breathing, not a smell.
  “Settled scans only” hides those; the export keeps every scan either way.
</div>

<script>
let MIN=60, SETTLED=true, D=null;
const WINS=[[15,'15 min'],[60,'1 h'],[240,'4 h'],[1440,'24 h']];
function el(id){return document.getElementById(id)}
function drawWin(){
  el('win').innerHTML = WINS.map(w=>`<button class="${MIN===w[0]?'on':''}" onclick="MIN=${w[0]};load()">${w[1]}</button>`).join('')
    + `<button class="${SETTLED?'on':''}" style="margin-left:10px" title="Hide the first scan after each rest - it reads high because the sensing surface recovered" onclick="SETTLED=!SETTLED;load()">settled scans only</button>`;
}
function mean(g){const v=g.filter(x=>x!=null&&x>0);return v.length?v.reduce((a,b)=>a+b,0)/v.length:null}
function colour(f){
  const st=[[0,[27,16,48]],[.3,[42,111,151]],[.55,[29,158,117]],[.8,[232,185,35]],[1,[224,78,58]]];
  f=Math.max(0,Math.min(1,f)); let a=st[0],b=st[st.length-1];
  for(let i=0;i<st.length-1;i++) if(f>=st[i][0]&&f<=st[i+1][0]){a=st[i];b=st[i+1];break}
  const t=(f-a[0])/Math.max(1e-6,b[0]-a[0]), c=a[1].map((v,i)=>Math.round(v+(b[1][i]-v)*t));
  return `rgb(${c[0]},${c[1]},${c[2]})`;
}
async function load(){
  drawWin();
  try{ D=await (await fetch(`/api/scans?minutes=${MIN}&settled=${SETTLED?1:0}`)).json(); }
  catch(e){ el('hdr').textContent='node unreachable'; return; }
  const n=D.scans.length;
  el('hdr').innerHTML=`${n} scans in view · fw ${D.fw} · sensor ${D.sensor_ok?'ok':'<span style="color:#e04e3a">not found</span>'} · ${D.seq} scans since boot`;
  el('stepLab').innerHTML=D.steps_c.map(c=>`<span>${c}°</span>`).join('');
  if(!n){ el('nowMsg').textContent='No scans yet. The first one arrives within about 11 seconds of boot.'; return; }
  const last=D.scans[n-1];
  const mx=Math.max(...last.g.filter(x=>x!=null));
  el('nowBars').innerHTML=last.g.map((v,i)=>`<i style="height:${v==null?2:Math.max(2,Math.round(80*v/mx))}px" title="step ${i} (${D.steps_c[i]}°): ${v==null?'—':v.toFixed(0)} kΩ"></i>`).join('');
  el('nowMsg').textContent=`burst position ${last.pos}${last.pos===1?' (first after a rest — reads high)':''} · ${last.t==null?'':last.t.toFixed(1)+' °C · '}${last.rh==null?'':last.rh.toFixed(0)+' % RH'}`;
  drawSpec(); drawLine(); patterns(); renderLabels();
}
function drawSpec(){
  const cv=el('spec'), c=cv.getContext('2d'), W=cv.width, H=cv.height, S=D.scans, n=S.length;
  c.fillStyle='#0d0b07'; c.fillRect(0,0,W,H);
  if(!n) return;
  const rows=10, rh=(H-16)/rows, cw=Math.max(1,W/n), lo=[], hi=[];
  for(let k=0;k<rows;k++){ const col=S.map(s=>s.g[k]).filter(v=>v!=null); lo[k]=Math.min(...col); hi[k]=Math.max(...col); }
  for(let i=0;i<n;i++) for(let k=0;k<rows;k++){
    const v=S[i].g[k]; if(v==null) continue;
    const span=Math.max(1e-6,hi[k]-lo[k]);
    c.fillStyle=colour(1-(v-lo[k])/span);
    c.fillRect(i*cw,k*rh,Math.ceil(cw),Math.ceil(rh));
  }
  c.fillStyle='#9a8a6a'; c.font='10px system-ui';
  for(let k=0;k<rows;k++) c.fillText(`${k}·${D.steps_c[k]}°`,3,k*rh+rh/2+3);
  const t0=S[0].epoch, t1=S[n-1].epoch;
  if(t0&&t1&&t1>t0) for(const L of (D.labels||[])){
    if(L.epoch<t0||L.epoch>t1) continue;
    const x=(L.epoch-t0)/(t1-t0)*W;
    c.fillStyle='#5fd39a'; c.beginPath(); c.moveTo(x,H-2); c.lineTo(x-5,H-13); c.lineTo(x+5,H-13); c.closePath(); c.fill();
  }
  el('specFrom').textContent = S[0].epoch ? new Date(S[0].epoch*1000).toLocaleString() : `${Math.round(S[0].ago_s/60)} min ago`;
}
function drawLine(){
  const cv=el('line'), c=cv.getContext('2d'), W=cv.width, H=cv.height, S=D.scans, n=S.length;
  c.fillStyle='#0d0b07'; c.fillRect(0,0,W,H);
  if(n<2) return;
  const m=S.map(s=>mean(s.g)).map(v=>v==null?null:v);
  const ok=m.filter(v=>v!=null).sort((a,b)=>a-b);
  const base=ok[Math.floor(ok.length*0.9)];
  const pts=m.map(v=>v==null?null:Math.max(0,Math.min(100,Math.round(100*(1-v/base)))));
  c.strokeStyle='rgba(255,255,255,.12)'; c.setLineDash([4,4]);
  [[15,'quiet'],[40,'mild'],[70,'strong']].forEach(([lv,t])=>{
    const y=H-8-(H-24)*lv/100;
    c.beginPath(); c.moveTo(34,y); c.lineTo(W-6,y); c.stroke();
    c.fillStyle='#9a8a6a'; c.font='10px system-ui'; c.fillText(t,4,y+3);
  });
  c.setLineDash([]);
  c.strokeStyle='#e8b923'; c.lineWidth=2; c.beginPath();
  pts.forEach((v,i)=>{ if(v==null) return;
    const x=34+(W-40)*i/(n-1), y=H-8-(H-24)*v/100;
    i?c.lineTo(x,y):c.moveTo(x,y); });
  c.stroke();
}
function patterns(){
  const S=D.scans, n=S.length, k=4;
  if(n<8){ el('pats').innerHTML='<div class="note">Needs at least eight scans to group shapes.</div>'; return; }
  const rows=S.map(s=>{ const m=mean(s.g)||1; return s.g.map(v=>v==null?1:v/m); });
  const order=rows.map((r,i)=>[mean(S[i].g)||0,i]).sort((a,b)=>a[0]-b[0]);
  let P=[0,1,2,3].map(j=>rows[order[Math.floor((j+.5)*n/k)][1]].slice());
  let A=new Array(n).fill(0);
  const dist=(a,b)=>a.reduce((s,x,i)=>s+(x-b[i])*(x-b[i]),0);
  for(let it=0;it<12;it++){
    const sum=P.map(()=>new Array(10).fill(0)), cnt=new Array(k).fill(0);
    rows.forEach((r,i)=>{ let bi=0,bd=Infinity; P.forEach((p,j)=>{const d=dist(r,p); if(d<bd){bd=d;bi=j}});
      A[i]=bi; cnt[bi]++; r.forEach((x,c)=>sum[bi][c]+=x); });
    P=P.map((p,j)=>cnt[j]?sum[j].map(x=>x/cnt[j]):p);
  }
  const cols=['#7A9BFF','#5fd39a','#F2A623','#E06AA3'], names=['A','B','C','D'];
  el('pats').innerHTML=P.map((p,j)=>{
    const c=A.filter(x=>x===j).length, mx=Math.max(...p);
    return `<div><div style="display:flex;justify-content:space-between;font-size:11px;margin-bottom:4px">
      <b style="color:${cols[j]}">Pattern ${names[j]}</b><span>${Math.round(100*c/n)}% of scans</span></div>
      <div class="bars">${p.map(v=>`<i style="height:${Math.max(2,Math.round(34*v/mx))}px;background:${cols[j]}"></i>`).join('')}</div></div>`;
  }).join('');
}
function renderLabels(){
  const L=(D.labels||[]).slice().reverse();
  el('lblList').innerHTML = L.length ? '<table>'+L.map(l=>
    `<tr><td>${l.epoch>1600000000?new Date(l.epoch*1000).toLocaleString():'(clock not set)'}</td><td>${l.text}</td>
     <td style="text-align:right"><span class="x" onclick="delLabel(${l.epoch})">✕</span></td></tr>`).join('')+'</table>'
    : '<div class="note">No labels yet. Add one whenever something is happening the nose should learn.</div>';
}
async function addLabel(){
  const t=el('lblIn').value.trim(); if(!t) return;
  await fetch('/api/label?text='+encodeURIComponent(t),{method:'POST'});
  el('lblIn').value=''; load();
}
async function delLabel(e){ await fetch('/api/label?del='+e,{method:'POST'}); load(); }
function dl(fmt){
  const l=el('xpLabel').value.trim(); if(!l){ alert('Give the session a label first.'); return; }
  const q=new URLSearchParams({label:l, kind:el('xpKind').value, minutes:el('xpMins').value,
                               format:fmt, settled:SETTLED?1:0});
  window.location='/api/export?'+q.toString();
}
load(); setInterval(load, 15000);
</script></body></html>)HTML";

static void handleRoot() { server.send_P(200, "text/html", PAGE); }

static void handleStatus() {
  char buf[320];
  snprintf(buf, sizeof(buf),
    "{\"ok\":true,\"node\":\"%s\",\"fw\":\"%s\",\"sensor\":%s,\"addr\":\"0x%02X\","
    "\"scans\":%lu,\"in_ring\":%u,\"uptime_s\":%lu,\"heap\":%lu,\"rssi\":%d,"
    "\"co2\":null,\"co2_note\":\"not measured: BSEC needs continuous IAQ to emit a "
    "CO2-equivalent, which would cost the fingerprint. Fit an SCD41 for real CO2.\"}",
    NODE_NAME, FW_VERSION, g_ok ? "true" : "false", g_addr,
    (unsigned long)g_seq, g_count, (unsigned long)((millis() - g_bootMs) / 1000UL),
    (unsigned long)ESP.getFreeHeap(), (int)WiFi.RSSI());
  sendJson(buf);
}

// ------------------------------------------------------------------ setup ---
void setup() {
  Serial.begin(115200);
  delay(300);
  g_bootMs = millis();
  Serial.printf("\n=== %s fw %s ===\n", NODE_NAME, FW_VERSION);

  WiFi.mode(WIFI_STA);
  WiFi.setHostname(NODE_NAME);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("[wifi] connecting");
  for (int i = 0; i < 60 && WiFi.status() != WL_CONNECTED; i++) { delay(500); Serial.print('.'); }
  if (WiFi.status() == WL_CONNECTED)
    Serial.printf("\n[wifi] %s\n[open] http://%s/\n", WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
  else
    Serial.println("\n[wifi] not connected - check WIFI_SSID / WIFI_PASS. Sensor still runs; page will not.");

  configTime(0, 0, "pool.ntp.org", "time.nist.gov");   // UTC; labels get real timestamps

  labelsLoad();
  g_ok = sensorBegin();

  server.on("/", handleRoot);
  server.on("/api/scans", handleScans);
  server.on("/api/label", handleLabel);
  server.on("/api/export", handleExport);
  server.on("/status", handleStatus);
  server.begin();
  Serial.println("[http] listening on :80  (/, /api/scans, /api/label, /api/export, /status)");
  Serial.println("[note] a new sensor needs 24-48 h of running before its gas readings settle.");
}

void loop() {
  if (g_ok && !envSensor.run()) {
    static uint32_t warned = 0;
    if (millis() - warned > 60000UL) { warned = millis(); bsecReport("run"); }
  }
  server.handleClient();
  delay(2);
}
