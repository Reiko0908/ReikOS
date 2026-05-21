#!/usr/bin/env bash

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

if [ -d "$SCRIPT_DIR/input" ] && [ -x "$SCRIPT_DIR/os" ]; then
  ROOT_DIR="$SCRIPT_DIR"
elif [ -d "$SCRIPT_DIR/../input" ] && [ -x "$SCRIPT_DIR/../os" ]; then
  ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
else
  echo "[ERROR] Cannot locate project root (expected input/ and os)."
  exit 1
fi

cd "$ROOT_DIR"

passed=0
failed=0
skipped=0

for i in {1..9}; do
  config_name="votien_memory_${i}"
  input_file="input/${config_name}"
  output_file="run/votien_memory_${i}.txt"

  if [ ! -f "$input_file" ]; then
    echo "[SKIP] Missing $input_file"
    skipped=$((skipped + 1))
    continue
  fi

  echo "[RUN ] ${config_name} -> $output_file"
  if ./os "$config_name" > "$output_file" 2>&1; then
    echo "[PASS] votien_memory_${i}"
    passed=$((passed + 1))
  else
    rc=$?
    echo "[FAIL] votien_memory_${i} (exit $rc)"
    failed=$((failed + 1))
  fi
done

echo "Summary: pass=$passed fail=$failed skip=$skipped"
echo "Outputs: run/votien_memory_1.txt .. run/votien_memory_9.txt"
