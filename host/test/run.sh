#!/bin/sh
# Checks that the verify programs pass the real files and reject the ones in
# host/test/ that are broken on purpose. See host/test/README.md.
#
# Run from anywhere, after gen_tables has written host/oracle/tables.h and
# host/oracle/tables.s:
#   sh host/test/run.sh

cd "$(dirname "$0")/../.." || exit 1
CC=${CC:-cc}
CFLAGS="-O2 -std=c99 -Wall -Wextra"
bin=$(mktemp -d) || exit 1
trap 'rm -rf "$bin"' 0 1 2 15
met=0
missed=0

# run NAME STATUS COMMAND...: runs COMMAND once, and it must exit with
# STATUS. Its output is kept for the want lines that follow.
run() {
    name=$1 status=$2
    shift 2
    out=$("$@" 2>&1)
    got=$?
    if [ "$got" -eq "$status" ]; then
        met=$((met + 1))
        echo "  ok    $name: exit $status"
    else
        missed=$((missed + 1))
        echo "  MISS  $name: wanted exit $status, got exit $got"
    fi
}

# want TEXT: the output of the last run must have a line containing TEXT.
want() {
    if printf '%s\n' "$out" | grep -qF -- "$1"; then
        met=$((met + 1))
        echo "  ok    $name: $1"
    else
        missed=$((missed + 1))
        echo "  MISS  $name: no line with \"$1\""
    fi
}

build() {
    out=$1
    shift
    $CC $CFLAGS "$@" -o "$bin/$out" || exit 1
}

build verify_tables host/verify/verify_tables.c
build verify_search host/verify/verify_search.c
for broken in missing_turn wrong_move check_always_ok read_wrong_p; do
    build "$broken" -DIDA_SOURCE="\"../test/ida_$broken.c\"" \
        host/verify/verify_search.c
done

echo "== The real files must pass =="
run tables.s 0 "$bin/verify_tables" host/oracle/tables.s
want "layout PASS, H2 PASS, H4 n/a, H1 PASS"
run ida.c 0 "$bin/verify_search" --depth 11
want "distance 11 only: H3 PASS, read_state PASS, T5 check PASS"

echo
echo "== The broken files must fail =="
run tables_ori_dist_over.s 1 \
    "$bin/verify_tables" host/test/tables_ori_dist_over.s
want "FAIL  ori_dist: every entry equals the reference"
want "FAIL  h <= d"
run tables_perm_turn_swapped.s 1 \
    "$bin/verify_tables" host/test/tables_perm_turn_swapped.s
want "FAIL  perm_turn, ori_turn: agree with solver.c"
run ida_missing_turn.c 1 "$bin/missing_turn" --depth 11
want "FAIL  2644 states searched"
run ida_wrong_move.c 1 "$bin/wrong_move" --depth 11
want "FAIL  replayed with solver.c's model"
want "FAIL  accepts every solution"
run ida_check_always_ok.c 1 "$bin/check_always_ok" --depth 11
want "FAIL  rejects every solution with its first move changed"
run ida_read_wrong_p.c 1 "$bin/read_wrong_p" --depth 11
want "FAIL  every state's digits give its p and o"

echo
echo "$met as expected, $missed not"
[ "$missed" -eq 0 ]
