#!/usr/bin/env python3
"""Flatten session files into one CSV: a row per scan, ten g0..g9 columns plus
label and context. This is the shape pandas, R, scikit-learn and the AI-Studio
import path all want.

    python tools/to_csv.py data/*/sessions/*.json -o all-scans.csv
"""
import json, sys, csv, glob, argparse

COLS = (["apiary_id", "hive_id", "unit_id", "label", "label_kind", "confidence",
         "t_utc"] + [f"g{i}_kohm" for i in range(10)] +
        [f"step{i}_c" for i in range(10)] +
        ["temp_c", "rh_pct", "hpa", "burn_in_hours", "firmware"])

def rows(path):
    with open(path, encoding="utf-8") as f:
        d = json.load(f)
    sen = d.get("sensor", {})
    steps = (sen.get("steps_c") or [None] * 10)[:10]
    for s in d.get("scans", []):
        g = (s.get("g_kohm") or [None] * 10)[:10]
        yield ([d.get("apiary_id"), d.get("hive_id"), sen.get("unit_id"), d.get("label"),
                d.get("label_kind"), d.get("confidence"), s.get("t_utc")] + list(g) +
               list(steps) + [s.get("temp_c"), s.get("rh_pct"), s.get("hpa"),
                              sen.get("burn_in_hours"), d.get("firmware")])

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("paths", nargs="+")
    ap.add_argument("-o", "--out", default="all-scans.csv")
    a = ap.parse_args()
    files = []
    for p in a.paths:
        files.extend(sorted(glob.glob(p)))
    n = 0
    with open(a.out, "w", newline="", encoding="utf-8") as fh:
        w = csv.writer(fh)
        w.writerow(COLS)
        for p in files:
            for r in rows(p):
                w.writerow(r); n += 1
    print(f"{n} scans from {len(files)} session(s) -> {a.out}")

if __name__ == "__main__":
    main()
