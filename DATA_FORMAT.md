# Session format (schema_version 1)

One JSON file per labelled recording session. Portable, human-readable, and
convertible to whatever a training tool wants.

```json
{
  "schema_version": 1,
  "apiary_id": "hive-observatory-pa",
  "hive_id": "hive3",
  "label": "tangerine oil",
  "label_kind": "reference",
  "confidence": "certain",
  "notes": "Cotton pad with 3 drops, glass cup inverted over the sensor.",
  "sensor": {
    "part": "BME688",
    "unit_id": "hive3-bme688-a",
    "heater_profile": "HP-354",
    "steps_c": [320, 100, 100, 100, 200, 200, 200, 320, 320, 320],
    "burn_in_hours": 96,
    "enclosure": "inside hive body, sensor head exposed, board in vented box"
  },
  "firmware": "hive_node_s3_bme688_v5.7.7",
  "started_utc": "2026-09-10T19:00:12Z",
  "ended_utc":   "2026-09-10T19:06:40Z",
  "scans": [
    {
      "t_utc": "2026-09-10T19:00:12Z",
      "g_kohm": [212.4, 188.0, 176.5, 171.2, 143.9, 139.7, 137.2, 121.8, 118.3, 116.0],
      "temp_c": 24.8, "rh_pct": 58.2, "hpa": 1004.1
    }
  ]
}
```

## Fields

| field | meaning |
|---|---|
| `label` | free text, lower case. Reuse existing labels where they fit. |
| `label_kind` | `reference` (a known substance, for validation), `colony` (a real hive state), or `ambient`. |
| `confidence` | `certain` or `unsure`. Be honest. |
| `g_kohm` | ten gas resistances in kΩ, **in heater-step order**. Lower = more volatiles at that temperature. |
| `steps_c` | the heater temperature of each step. Required: a fingerprint means nothing without it. |
| `unit_id` | stable id for the physical sensor. Lets a model account for unit-to-unit variation. |
| `burn_in_hours` | hours the sensor had been powered before this session. Bosch advises 24–48 h minimum. |

## Why kΩ and not the raw ADC

Raw counts are meaningless across firmware versions. Kilohms are what the BME68x driver
reports and what AI-Studio works in.

## Converting for training

```
python tools/to_csv.py data/*/sessions/*.json -o all-scans.csv
```

produces one row per scan with `g0..g9` columns plus label and context — directly
loadable in pandas, R, or a spreadsheet, and the shape most ML tooling expects.

Bosch **BME AI-Studio** imports its own `.bmerawdata` files. The community project
[notyourtree/bme688-aistudio-setup](https://github.com/notyourtree/bme688-aistudio-setup)
documents importing raw breakout data into AI-Studio; the CSV above carries every field
that path needs. A direct `.bmerawdata` writer is an open task — see the issues.

## `site` and `conditions` (added by the exporter)

Both are filled in automatically and both matter more than they look:

```json
"site": { "region": "Pocono Plateau, Pennsylvania, USA",
          "climate": "Dfb (warm-summer humid continental)",
          "elevation_m": 280, "hive_type": "Langstroth 10-frame" },
"conditions": { "in_hive_temp_c": 25.0, "in_hive_rh_pct": 58.0, "in_hive_hpa": 1004.0,
                "outdoor_temp_c": 21.9, "outdoor_conditions": "partly cloudy",
                "wind_mph": 6.5, "pressure_trend": "rising" }
```

A metal-oxide sensor's baseline moves with temperature and humidity. Two fingerprints
recorded at 15 °C and 30 °C differ even with identical air, so a session without
conditions cannot be compared with anyone else's - and a model trained across apiaries
needs them as inputs, not footnotes.

Keep `site.region` coarse. Region and climate zone, never an address.
