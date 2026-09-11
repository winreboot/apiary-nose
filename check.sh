#!/bin/sh
# Full validation (schema + sanity) without installing anything on the NAS.
cd "$(dirname "$0")" || exit 1
docker run --rm -v "$PWD":/w -w /w python:3.12-slim \
  sh -c "pip install -q jsonschema 2>/dev/null && python tools/validate.py 'data/*/sessions/*.json'"
