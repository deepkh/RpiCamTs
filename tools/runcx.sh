#!/usr/bin/env bash
set -euo pipefail

PLAN_FILE="$1"
LOG_FILE="./log/codex_$(date +%Y%m%d_%H%M%S).log"

if ! command -v codex >/dev/null 2>&1; then
  echo "Error: codex command not found."
  exit 1
fi

if [ ! -f "$PLAN_FILE" ]; then
  echo "Error: $PLAN_FILE not found in current directory: $(pwd)"
  exit 1
fi

echo "Running Codex without approval prompts..."
echo "Log file: $LOG_FILE"

codex exec \
  --sandbox workspace-write \
  -c approval_policy='"never"' \
  - < "$PLAN_FILE" 2>&1 | tee "$LOG_FILE"
