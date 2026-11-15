#!/usr/bin/env bash
# ------------------------------------------------------------------------
#   Three real daemons on this machine (ports 47911-47913, their own
#   state dirs and control sockets), driven through the CLI:
#
#   1. alpha pairs bravo by code and address
#   2. bravo pairs charlie, charlie finding bravo by broadcast; alpha
#      learns charlie from bravo's gossip and finds it by beacon
#   3. a wrong code fails and burns the code
#   4. a stranger with a copy of the roster is refused
#   5. alpha revokes charlie; bravo hears it; charlie is refused
#   6. alpha restarts and its links come back
#
#   SYN-OS     : The Syntax Operating System
#   Component  : SYN-NODE (tests)
#   Author     : William Hayward-Holland (Syntax990)
#   License    : MIT License
# ------------------------------------------------------------------------
set -u
NODED="$1"
NODE="$2"
DIAL="${3:-$(dirname "$NODE")/test_dial}"
T="$(mktemp -d)"
FAILS=0
PIDS=()

cleanup() {
  for p in "${PIDS[@]}"; do kill "$p" 2>/dev/null; done
  wait 2>/dev/null
  rm -rf "$T"
}
trap cleanup EXIT

port_of() { case "$1" in alpha) echo 47911;; bravo) echo 47912;; charlie) echo 47913;; stranger) echo 47914;; esac; }

env_for() {
  mkdir -p "$T/$1"
  [ -f "$T/$1/name" ] || echo "$1" > "$T/$1/name"
  echo "SN_HOME=$T/$1 SN_PORT=$(port_of "$1") SN_SOCK=$T/$1.sock"
}

start() {
  env $(env_for "$1") "$NODED" >>"$T/$1.log" 2>&1 &
  PIDS+=($!)
  eval "PID_$1=$!"
  for _ in $(seq 50); do [ -S "$T/$1.sock" ] && return; sleep 0.1; done
  echo "FAIL: $1 didn't start"; FAILS=$((FAILS+1))
}

stop() {
  local p; eval "p=\$PID_$1"
  kill "$p" 2>/dev/null; wait "$p" 2>/dev/null
}

node() { local who="$1"; shift; env $(env_for "$who") "$NODE" "$@"; }

check() {
  if eval "$2"; then echo "ok:   $1"; else echo "FAIL: $1"; FAILS=$((FAILS+1)); fi
}

state_of() { node "$1" nodes --raw | awk -F'|' -v n="$2" '$1==n {print $3}'; }
key_of() { node "$1" status | awk '$1=="key" {print $2}'; }

# waits up to $3 seconds for state_of $1 $2 to be $4
wait_state() {
  for _ in $(seq $(( $3 * 5 ))); do
    [ "$(state_of "$1" "$2")" = "$4" ] && return 0
    sleep 0.2
  done
  return 1
}

# start_pair <member>: runs pair in the background (sets PAIRPID, CODE)
start_pair() {
  : >"$T/pair-$1.out"
  node "$1" pair >"$T/pair-$1.out" 2>&1 &
  PAIRPID=$!
  CODE=""
  for _ in $(seq 50); do
    CODE=$(awk '/^Pairing code:/ {print $3 $4}' "$T/pair-$1.out")
    [ -n "$CODE" ] && return
    sleep 0.1
  done
}

start alpha; start bravo; start charlie

# 1
start_pair alpha; code=$CODE
check "alpha shows a code" '[ -n "$code" ]'
node bravo join "$code" 127.0.0.1:47911 >"$T/join-bravo.out" 2>&1
check "bravo joins alpha by address" 'grep -q "Joined" "$T/join-bravo.out"'
wait $PAIRPID
check "alpha's pair reports bravo" 'grep -q "Paired: bravo" "$T/pair-alpha.out"'
check "alpha sees bravo online" 'wait_state alpha bravo 10 online'
check "bravo sees alpha online" 'wait_state bravo alpha 10 online'

# 2
start_pair bravo; code=$CODE
node charlie join "$code" >"$T/join-charlie.out" 2>&1
check "charlie finds bravo by broadcast and joins" 'grep -q "Joined" "$T/join-charlie.out"'
wait $PAIRPID
check "alpha learns charlie by gossip" 'wait_state alpha charlie 15 offline || wait_state alpha charlie 1 online'
check "alpha reaches charlie (beacon)" 'wait_state alpha charlie 20 online'
check "charlie sees alpha online" 'wait_state charlie alpha 20 online'

# 3
start stranger
start_pair alpha; code=$CODE
wrong=$(printf '%08d' $(( (10#$code + 1) % 100000000 )))
node stranger join "$wrong" 127.0.0.1:47911 >"$T/join-wrong.out" 2>&1
check "wrong code refused" '! grep -q "Joined" "$T/join-wrong.out"'
wait $PAIRPID
check "alpha reports the wrong code" 'grep -q "wrong code" "$T/pair-alpha.out"'
node stranger join "$code" 127.0.0.1:47911 >"$T/join-burned.out" 2>&1
check "the right code no longer works (burned)" '! grep -q "Joined" "$T/join-burned.out"'

# 4
cp "$T/bravo/roster" "$T/stranger/roster"
akey=$(key_of alpha)
check "stranger with a copied roster refused" '! env SN_HOME="$T/stranger" "$DIAL" 127.0.0.1 47911 "$akey" >/dev/null'
check "bravo itself accepted (control)" 'env SN_HOME="$T/bravo" "$DIAL" 127.0.0.1 47911 "$akey" >/dev/null'

# 5
node alpha revoke charlie >"$T/revoke.out" 2>&1
check "alpha revokes charlie" 'grep -q "Revoked charlie" "$T/revoke.out"'
check "alpha shows charlie revoked" '[ "$(state_of alpha charlie)" = revoked ]'
check "bravo hears the revocation" 'wait_state bravo charlie 10 revoked'
check "charlie refused by alpha" '! env SN_HOME="$T/charlie" "$DIAL" 127.0.0.1 47911 "$akey" >/dev/null'
bkey=$(key_of bravo)
check "charlie refused by bravo" '! env SN_HOME="$T/charlie" "$DIAL" 127.0.0.1 47912 "$bkey" >/dev/null'

# 6
stop alpha
check "bravo sees alpha go" 'wait_state bravo alpha 50 offline'
start alpha
check "alpha back: link to bravo returns" 'wait_state alpha bravo 20 online'

for p in "${PIDS[@]}"; do kill "$p" 2>/dev/null; done
wait 2>/dev/null
check "no sanitizer reports in daemon logs" '! grep -qE "AddressSanitizer|LeakSanitizer|runtime error" "$T"/*.log'

if [ "$FAILS" -ne 0 ]; then
  echo "test_mesh: $FAILS failure(s); logs:"
  for f in "$T"/*.log; do echo "== $f"; cat "$f"; done
  exit 1
fi
echo "test_mesh: ok"
