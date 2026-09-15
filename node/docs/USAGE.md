# Recording a session

## The rhythm

1. **Let it settle.** With nothing present, let a few bursts go by. This is your
   baseline, and it matters as much as anything interesting.
2. **Label the start.** Type `clear room` and press Add.
3. **Introduce the smell.** A cotton pad with a few drops under an inverted glass works
   well. Label it — `coffee`, `tangerine oil`, `nectar flow`.
4. **Leave it for several scans.** At about 11 s per scan and a rest between bursts, give
   it two or three minutes.
5. **Remove it and label that too.** The recovery curve is as informative as the rise.

## Reading the page

**Fingerprint now** — ten bars, one per heater step, with its temperature underneath.
Lower resistance means more volatiles reacting at that temperature. The cool steps
(100 °C) normally read much higher than the hot ones; that is the physics, not a fault.

**Fingerprint over time** — one column per scan, each row a heater step, coloured from
blue (quiet) to red (strong) *relative to this window*. A smell arriving shows as a
vertical band; which rows light up is the shape that distinguishes one smell from
another.

**Smell intensity** — the mean resistance inverted against the cleanest air in view, so
0 % is the quietest and 100 % the strongest. Good for spotting when something happened;
useless for saying what.

**Recurring patterns** — every scan's shape with intensity divided out, grouped into four
clusters. This is the most useful part before any training exists: if a new pattern
starts appearing, something changed, even though nothing has been taught yet.

**Settled scans only** — leave this on. It keeps one scan per burst, the last before the
rest. Dropping just the first scan is not enough: resistance climbs through the *whole*
burst as the surface recovers — measured on one node, 10368 kΩ at position 3 against
13152 kΩ at position 5, 27 % apart with nothing in the air changing. Averaging a varying
mix of positions puts spikes in any trend built from it.

## Exporting

Fill in a label, pick a kind, set how many minutes back, and download.

- **reference** — a known substance (oils, syrup, smoker fuel). Reproducible, good for
  checking the sensor still behaves.
- **colony** — a real hive state (nectar flow, robbing, brood).
- **ambient** — baseline air, nothing presented.

JSON carries the full session with every scan, its burst position and the heater profile.
CSV is one row per scan with `g0…g9` columns — the shape pandas, R and most ML tooling
expect.

## What makes a session worth keeping

- **Record the boring class.** Ambient air needs as many sessions as the interesting
  states, or a classifier has nothing to contrast against.
- **Repeat.** One presentation is an anecdote. Ten, across different days and
  temperatures, are a shape.
- **Be honest.** Not sure it was robbing? Say so. A confident wrong label is worse than
  no label.
- **Mixtures are their own class.** Two smells together do not produce the sum of their
  fingerprints — train the combination separately if it matters.

## Contributing

The export format matches the [Apiary Nose](https://github.com/winreboot/apiary-nose)
dataset. Validate a file with that repository's `tools/validate.py`, then open a pull
request or attach it to a submission issue.

## The API, if you want to script it

| endpoint | does |
|---|---|
| `GET /api/scans?minutes=60&settled=1` | scans in view, with labels |
| `POST /api/label?text=coffee` | stamp the current moment |
| `POST /api/label?del=<epoch>` | remove a label |
| `GET /api/export?label=x&kind=reference&minutes=20&format=json` | download a session |
| `GET /status` | uptime, heap, sensor address, scan count |

Labels survive a reboot; scans do not — the ring is in RAM. Export anything you want to
keep before power-cycling the board.

## CO₂

There is none, deliberately. The BME688's CO₂-equivalent needs BSEC's IAQ mode running
continuously to produce anything, which would mean giving up the fingerprint. Fourteen
scheduled windows on a real node produced nothing at all before that was understood.

If you want CO₂ alongside smell, add an **SCD41** on the same SDA/SCL pair at address 0x62.
It is a true NDIR measurement, it runs independently, and the fingerprint keeps scanning.
