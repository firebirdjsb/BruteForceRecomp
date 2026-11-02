#!/bin/sh
set -eu
cd -- "$(dirname -- "$0")"
for launcher in ./BruteForceLauncher ./.local/launcher-build/native_launcher/BruteForceLauncher; do
    if [ -x "$launcher" ]; then exec "$launcher" "$@"; fi
done
echo "Native launcher not built. See the Desktop application section in README.md." >&2
exit 1
