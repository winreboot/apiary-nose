# Wiring

![BME688 to ESP32-S3](wiring.svg)

Four wires between the sensor and the board, plus two pull-up resistors and — for
anything longer than a short jumper — one capacitor.

| BME688 pin | goes to | note |
|---|---|---|
| VCC | ESP32-S3 **3V3** | 3.3 V only |
| GND | ESP32-S3 **GND** | |
| SCL / SCK | **GPIO13** | clock |
| SDA / MOSI | **GPIO21** | data — **the pin marked MOSI is the I²C data line** on these boards |
| MISO | — | leave empty (SPI only) |
| CS | — | leave empty (SPI only) |

Different GPIOs are fine; change `I2C_SDA` and `I2C_SCL` at the top of the sketch.

## Pull-ups

I²C needs a pull-up on each line. **2.2 kΩ from SDA to 3V3 and from SCL to 3V3**, fitted
at the **sensor end** of the cable. Many breakouts already have them — check the board
before adding more, since two sets in parallel can over-pull a long bus.

## Capacitor

If the sensor is more than about 30 cm from the board, put **220 µF across VCC and GND at
the sensor's own pins**, long leg to VCC. The heater pulses draw current in bursts, and a
thin pair of wires drops enough voltage to reset the chip mid-measurement.

## Address

Most modules answer at **0x77**; some have a switch or a solder bridge for **0x76**. The
firmware tries both and prints which it found. If neither answers it scans the whole bus
and tells you what it saw.

## Handling

**Do not touch the metal cap on the sensor.** Skin oils contaminate the gas element, and
the contamination does not wash off. Handle the board by its edges.

## Placement

The element is sensitive to draughts and to its own heat. Give it still air, keep it away
from anything warm, and if it lives in an enclosure, vent the enclosure rather than
sealing it — the sensor needs the air it is measuring to actually reach it.
