#!/usr/bin/env sh
set -e

export CC24_OPTIONS=/data/options.json
export CC24_ROOT=/opt/dcc24
export CC24_CONFIG=/config

echo "[dcc24] starting on :8099"
exec python3 /opt/dcc24/server.py
