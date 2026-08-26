#!/usr/bin/with-contenv sh
#
# WITH-CONTENV, not a plain shell. s6-overlay runs services with a SCRUBBED
# environment: the container's variables are parked in
# /run/s6/container_environment and a service sees none of them unless it is
# started this way. SUPERVISOR_TOKEN is one of them, and without it every write
# to the wall fails with "not running as an add-on" while the add-on otherwise
# looks perfectly healthy.
set -e

export CC24_OPTIONS=/data/options.json
export CC24_ROOT=/opt/dcc24
export CC24_CONFIG=/config

echo "[dcc24] starting on :8099"
exec python3 /opt/dcc24/server.py
