# Apiary Nose

**An open dataset of what beehives smell like — and an invitation to help build it.**

Beekeepers already use their noses. The sweet smell of a nectar flow, the sharp banana
note of alarm pheromone, the foul smell that makes you close a hive and reach for the
phone. We know these things matter. We have never been able to record them.

This project is an attempt to change that, and it needs more than one apiary to work.

---

## The sensor

The [Bosch **BME688**](https://www.bosch-sensortec.com/products/environmental-sensors/gas-sensors/bme688/)
is a 4-in-1 environmental sensor — temperature, humidity, pressure, and a metal-oxide
gas element — in a package a few millimetres across. The gas element is the interesting
part.

Most gas sensors give you one number: a resistance that falls as volatile compounds
react on a heated surface. That tells you *how much* is in the air, not *what*. The
BME688 adds a programmable heater, and this is the trick that makes the project
possible: **different compounds react most strongly at different temperatures.**

So instead of one measurement, the sensor sweeps its heater through ten temperatures in
sequence — in the standard profile: 320 °C, 100 °C, 100 °C, 100 °C, 200 °C, 200 °C,
200 °C, 320 °C, 320 °C, 320 °C — and reports the resistance at each step. That row of
ten numbers is a **fingerprint**. Citrus oil suppresses the hot steps hard and the cool
steps barely at all. Something else will lean the other way. The shape is the
information.

A scan takes about eleven seconds. Run it continuously in a hive and you get a
fingerprint every eleven seconds, all season.

### What the sensor cannot do

Being straight about this saves everyone disappointment:

- **It does not identify compounds.** It has no idea what "orange" is. It reports how a
  mixture of unknown volatiles happens to affect one heated surface. Meaning comes only
  from labelled examples.
- **It has no absolute scale.** Resistances depend on the individual chip, its burn-in
  age, the enclosure, the humidity and the temperature. Two sensors in the same hive
  will not agree on numbers — though they will agree on *shapes*, which is what a model
  learns.
- **Smells do not add up.** Two compounds present together compete for the same sensing
  surface. A + B is not A's fingerprint plus B's. Knowing two smells does not let you
  predict the third; a mixture has to be learned as its own class.
- **It needs burn-in.** Bosch advises 24–48 hours of continuous running on a fresh
  sensor before the gas readings mean anything.

---

## What we are trying to do

Bosch ships a tool, BME AI-Studio, that trains a small classifier from labelled
fingerprints and compiles it into firmware. The workflow is sound. The obstacle is
data: a model trained on **one** sensor in **one** hive in **one** climate learns that
sensor's quirks as much as the smell, and degrades when it meets another. Bosch's own
development kit carries eight sensors, precisely because generalisation needs variety.

No public dataset of hive smells appears to exist. Coffee versus clean air, yes — that
is the tutorial example. Beehives, no.

So the goal here is simple and long:

1. **Collect** labelled fingerprints from working hives — many sensors, many apiaries,
   many climates, across whole seasons.
2. **Publish** them in a plain, documented, vendor-neutral format that outlives any one
   tool.
3. **Train** models anyone can use, and be honest about how well they actually work.

The states worth recognising, roughly in order of usefulness:

| State | Why it matters |
|---|---|
| Ambient hive air | The baseline everything else is measured against. Unglamorous and essential. |
| Nectar flow | Confirms forage without opening the hive. |
| Brood rearing | Colony health at a glance. |
| Robbing | Early warning, when intervention still helps. |
| Queenlessness | Currently needs an inspection to detect. |
| Chalkbrood / EFB / AFB | The one every beekeeper wants, and the hardest to collect ethically. |
| Varroa treatment residue | Distinguishing treatment from disease. |

We are at the beginning of that list, not the end.

---

## Please contribute — this is the ask

If you run a BME688 near a colony, **your data is valuable**, and it is valuable even
if it looks boring. Especially if it looks boring.

A session is a stretch of scans with one honest label. What makes them worth having:

- **Record the boring class.** Ambient hive air needs as many sessions as the dramatic
  states. Without contrast, a classifier learns nothing.
- **Repeat.** One presentation of a smell is an anecdote. Ten, across different days and
  temperatures, are a shape.
- **Label honestly.** Not certain it was robbing? Mark it `unsure` and say why in the
  notes. A confident wrong label is worse than no label at all.
- **Say what your setup is.** Sensor unit, burn-in hours, where it sits in the hive.
  Numbers without that context cannot be compared with anyone else's.

**Reference smells are useful too.** Essential oils, sugar syrup, smoker fuel — anything
reproducible. They validate that a recording chain works and give every contributor a
common yardstick across different chips.

### How to submit

1. Export a session as JSON — see [DATA_FORMAT.md](DATA_FORMAT.md).
2. Validate it: `python tools/validate.py my-session.json`
3. Open a pull request adding it to `data/<your-apiary-id>/sessions/`,
   **or** attach the file to a
   [Smell submission issue](../../issues/new?template=smell-submission.yml) if git is
   not your thing.

Not running a sensor yet but curious? Say hello in
[Discussions](../../discussions). Questions about reading fingerprints, hardware
choices, or whether a pattern you are seeing means anything are all welcome — including
from people who think this whole idea is unlikely to work. Skepticism improves it.

---

## What is in this repository

```
data/<apiary-id>/
  apiary.json              site, hives and sensor units
  sessions/*.json          labelled recording sessions
schema/session-v1.schema.json
tools/validate.py          check a file before submitting
tools/to_csv.py            flatten sessions to CSV for training tools
docs/                      the project page
```

The format is documented in [DATA_FORMAT.md](DATA_FORMAT.md) and enforced by CI on
every pull request. It is plain JSON — ten resistances, the heater temperature of each
step, ambient conditions, a label. Nothing proprietary, nothing that stops working when
a vendor tool changes.

> The files under `data/example-synthetic/` are **generated, not measured**. They exist
> to show the shape of a valid file. Do not train on them.

## Current state

Early days. The first sessions come from the Dusk Apiary Observatory in the Pocono
Plateau, Pennsylvania — a three-hive setup with hive weight, CO₂, acoustics, bee
counting and a weather station, where the BME688 sits inside hive three. Reference
smells first, colony states as the season allows.

If you are reading this in year two and the dataset is still small, that means the idea
did not catch on. If it is large, it is because people who did not have to share their
data chose to.

## Licence

Data: [CC BY 4.0](LICENSE-DATA) — use it, publish on it, credit the apiary it came
from. Tools: [MIT](LICENSE).

## Build a sensor

[`node/`](node/) has everything for a standalone recorder: a wiring diagram, firmware for
an ESP32-S3, and a web page served by the board itself — no server, no database. Its
exports match the session format used here, so anything it records can be contributed
straight back.

