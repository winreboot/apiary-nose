# Building and flashing

## What you need

- An **ESP32-S3** development board (any of the common devkits)
- A **BME688** breakout (Adafruit, SparkFun, Seengreat/Xicoolee and the generic CJMCU
  boards all work)
- Two 2.2 kΩ resistors, one 220 µF capacitor, jumper wires
- A USB-C cable that carries data — not a charge-only one

## Arduino IDE

1. Install the **Arduino IDE 2.x**.
2. **File → Preferences → Additional board manager URLs**, add:
   `https://espressif.github.io/arduino-esp32/package_esp32_index.json`
3. **Tools → Board → Boards Manager**, search `esp32`, install **esp32 by Espressif**.
4. **Tools → Board → ESP32 Arduino → ESP32S3 Dev Module**.
5. **Sketch → Include Library → Manage Libraries**, search **`bsec2`**, install
   **bsec2 by Bosch Sensortec**. When it offers the dependency **BME68x Sensor
   library**, choose *Install all*.

That is the only library. Everything else ships with the ESP32 core.

## Flash it

1. Open `firmware/bme688_nose/bme688_nose.ino`.
2. Set your network at the top:
   ```cpp
   #define WIFI_SSID  "YOUR_WIFI"
   #define WIFI_PASS  "YOUR_PASSWORD"
   ```
3. Select the port under **Tools → Port** and press Upload.
4. Open **Tools → Serial Monitor** at **115200 baud**.

You should see:

```
=== bme688-nose fw 1.0.0 ===
[wifi] connecting.....
[wifi] YourNetwork
[open] http://192.168.1.50/
[bme688] found at 0x77, BSEC 2.6.1.0, scan mode (10 heater steps, ~11 s)
[http] listening on :80  (/, /api/scans, /api/label, /api/export, /status)
[nose] scan 1 (10/10 steps, burst pos 1, +0s) mean 214 kOhm
```

Browse to the address it printed. **`10/10 steps` is the number to check** — that means
the whole fingerprint is being captured.

## If it does not work

**`nothing answers at 0x77 or 0x76`** — the firmware then scans the whole bus and lists
what it found. Nothing at all means power or the SDA/SCL pair; other addresses but not
the sensor means the address switch, or SDA and SCL swapped.

**`x/10 steps` with x below 10** — some heater steps are not arriving. Usually a marginal
supply: fit the capacitor at the sensor, or shorten the cable.

**Compile error `bsec2.h: No such file or directory`** — the library is not installed;
see step 5 above.

**Link error `undefined reference to bsec_…`** — a known quirk of Bosch's precompiled
library with some core versions; updating the ESP32 core to 2.0.14 or newer normally
fixes it.

**Readings that drift for a day** — that is burn-in, not a fault. Leave it running.
