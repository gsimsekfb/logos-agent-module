#!/usr/bin/env bash
# Stage 4: runtime smoke test — loads the built plugin in a real logoscore
# daemon and calls the skills (same calls as the manual 1.6 test).
#
# Usage: tests/smoke_logoscore.sh [path-to-logoscore]
# Needs: result/lib/agent_module_plugin.so (nix build) and manifest.json.
set -u

LOGOSCORE="${1:-logoscore}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
WORK="$(mktemp -d)"
MODULES="$WORK/modules"
export HOME="$WORK/home"   # isolated ~/.logoscore state
mkdir -p "$HOME" "$MODULES/agent_module"

cp "$ROOT/result/lib/agent_module_plugin.so" "$ROOT/manifest.json" "$MODULES/agent_module/"

FAILED=0
cleanup() { "$LOGOSCORE" stop >/dev/null 2>&1; kill "$DAEMON_PID" 2>/dev/null; rm -rf "$WORK"; }
trap cleanup EXIT

"$LOGOSCORE" daemon -m "$MODULES" >"$WORK/daemon.log" 2>&1 &
DAEMON_PID=$!
sleep 3

# check <name> <expected substring> <cmd...>
check() {
  local name="$1" expect="$2"; shift 2
  local start=$SECONDS out
  out="$(timeout 15 "$@" 2>&1)"
  local took=$((SECONDS - start))
  if [[ "$out" == *"$expect"* && $took -le 8 ]]; then
    echo "PASS: $name (${took}s)"
  else
    echo "FAIL: $name (${took}s) — expected '$expect', got:"; echo "$out"
    FAILED=1
  fi
}

timeout 30 "$LOGOSCORE" load-module agent_module || { echo "FAIL: load-module"; cat "$WORK/daemon.log"; exit 1; }

check "listSkills"            "meta.status"        "$LOGOSCORE" call agent_module listSkills
check "getStatus"             "skills"             "$LOGOSCORE" call agent_module getStatus
check "meta.status"           "unavailable"        "$LOGOSCORE" call agent_module executeSkill meta.status '{}'
check "wallet.balance"        "wallet module not connected" "$LOGOSCORE" call agent_module executeSkill wallet.balance '{}'
check "echo"                  "Echo: hello world"  "$LOGOSCORE" call agent_module executeSkill echo 'hello world'
check "unknown skill"         "Unknown skill: nope" "$LOGOSCORE" call agent_module executeSkill nope '{}'

# Daemon must still be alive (regression: 1.6 wallet timeout segfault)
if kill -0 "$DAEMON_PID" 2>/dev/null; then echo "PASS: daemon alive"; else echo "FAIL: daemon crashed"; cat "$WORK/daemon.log"; FAILED=1; fi

exit $FAILED
