#!/usr/bin/env python3
"""Validate an Apiary Nose session file.

    python tools/validate.py data/*/sessions/*.json

Checks the schema and then the things a schema cannot: that fingerprints really
carry ten steps, that resistances are plausible, and that labels are not empty
placeholders. Exits non-zero if anything fails, so CI can use it directly.
"""
import json, sys, glob, datetime

HERE = __import__("os").path.dirname(__import__("os").path.abspath(__file__))
SCHEMA = __import__("os").path.join(HERE, "..", "schema", "session-v1.schema.json")

def load_schema():
    with open(SCHEMA, encoding="utf-8") as f:
        return json.load(f)

def check(path, schema):
    problems, warnings = [], []
    try:
        with open(path, encoding="utf-8") as f:
            doc = json.load(f)
    except Exception as e:
        return [f"not valid JSON: {e}"], []

    try:
        import jsonschema
        v = jsonschema.Draft202012Validator(schema)
        for err in sorted(v.iter_errors(doc), key=lambda e: e.path):
            loc = "/".join(str(p) for p in err.path) or "(root)"
            problems.append(f"{loc}: {err.message}")
    except ImportError:
        warnings.append("jsonschema not installed - structural check skipped "
                        "(pip install jsonschema)")

    scans = doc.get("scans") or []
    if scans:
        full = sum(1 for s in scans
                   if len([g for g in (s.get("g_kohm") or []) if g not in (None, 0)]) >= 10)
        if full == 0:
            problems.append("no scan has all ten heater steps - older firmware "
                            "recorded only one step; those files are not usable for training")
        elif full < len(scans) * 0.8:
            warnings.append(f"only {full}/{len(scans)} scans have all ten steps")
        flat = [g for s in scans for g in (s.get("g_kohm") or []) if isinstance(g, (int, float))]
        if flat:
            lo, hi = min(flat), max(flat)
            if hi > 5000:
                warnings.append(f"resistances up to {hi:.0f} kOhm - unusually high; "
                                "check the unit is kOhm and not ohms")
            if lo <= 0:
                problems.append("a resistance is zero or negative")
            if hi == lo:
                problems.append("every resistance is identical - the sensor was not responding")

    lbl = (doc.get("label") or "").strip().lower()
    if lbl in {"test", "todo", "label", "smell", "unknown", ""}:
        problems.append(f"label {lbl!r} says nothing - name what was actually present")
    if doc.get("label_kind") == "colony" and not doc.get("notes"):
        warnings.append("a colony state without notes is hard for anyone else to interpret")

    sen = doc.get("sensor") or {}
    if not sen.get("burn_in_hours"):
        warnings.append("no burn_in_hours - readings from a fresh sensor drift for 24-48 h")

    for key in ("started_utc", "ended_utc"):
        val = doc.get(key)
        if val:
            try:
                datetime.datetime.fromisoformat(val.replace("Z", "+00:00"))
            except ValueError:
                problems.append(f"{key} is not an ISO-8601 timestamp: {val!r}")
    return problems, warnings

def main(argv):
    paths = []
    for a in argv or ["data/*/sessions/*.json"]:
        paths.extend(sorted(glob.glob(a)))
    if not paths:
        print("no files matched"); return 1
    schema = load_schema()
    bad = 0
    for p in paths:
        problems, warnings = check(p, schema)
        if problems:
            bad += 1
            print(f"FAIL {p}")
            for m in problems: print(f"   - {m}")
        else:
            print(f"ok   {p}")
        for m in warnings: print(f"   ~ {m}")
    print(f"\n{len(paths) - bad}/{len(paths)} file(s) valid")
    return 1 if bad else 0

if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
