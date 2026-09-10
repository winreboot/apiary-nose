---
layout: default
title: Apiary Nose
---

# Apiary Nose

**An open dataset of what beehives smell like.**

A Bosch BME688 steps its heater through ten temperatures every scan. Each volatile
compound reacts differently at each temperature, so one scan yields a ten-number
*fingerprint* rather than a single air-quality figure. Collect enough fingerprints with
honest labels and a classifier can learn to tell a nectar flow from a robbing event.

The catch is that a metal-oxide sensor has no absolute scale: its readings depend on
the individual chip, its age, its enclosure and the weather. A model trained on one
sensor degrades on another. Getting past that needs data from many sensors, many hives
and many seasons — more than any one beekeeper can gather. Bosch's own development kit
carries eight sensors for this reason.

No public dataset of hive smells appears to exist. This is an attempt to start one.

## Contribute

1. Record a labelled session on your own hardware.
2. Validate it: `python tools/validate.py my-session.json`
3. Open a pull request, or attach the file to a
   [Smell submission issue]({{ site.github.repository_url }}/issues/new?template=smell-submission.yml).

The [session format]({{ site.github.repository_url }}/blob/main/DATA_FORMAT.md) is plain
JSON: ten resistances per scan, the heater temperature of each step, the ambient
conditions, and a label. Nothing exotic, nothing vendor-locked.

## What makes a session worth having

- **The boring class matters.** "Ambient hive air" needs as many sessions as the
  interesting states, or a model has nothing to contrast against.
- **Repetition beats variety.** Ten presentations across different days teach a shape;
  one teaches noise.
- **Honest labels.** Not sure it was robbing? Mark it `unsure` and say why. A wrong
  label is worse than no label.
- **Mixtures are their own class.** Two smells together do not produce the sum of their
  fingerprints — they compete for the same sensing surface. If a combination matters to
  you, record it as its own class.

## Discuss

Questions, fingerprint interpretation, sensor troubleshooting and training results
belong in [Discussions]({{ site.github.repository_url }}/discussions).

## Licence

Data under [CC BY 4.0]({{ site.github.repository_url }}/blob/main/LICENSE-DATA) — use
it freely, credit the apiary it came from. Tools under MIT.
