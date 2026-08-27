#!/bin/sh
# run.sh - full benchmark suite.
#
# Builds the C helpers if needed, then runs each scenario and
# writes results to results/<tag>-<date>.txt. Run on an idle
# machine for baseline numbers; repeat after kernel changes.

set -eu

DIR="$(dirname "$0")"
cd "$DIR"

[ -x wakeup ] || cc -O2 -pthread -o wakeup wakeup.c
[ -x spawn ] || cc -O2 -o spawn spawn.c
[ -x iostall ] || cc -O2 -pthread -o iostall iostall.c
[ -x tone ] || cc -O2 -o tone tone.c -lm

NC=$(sysctl -n hw.ncpu 2>/dev/null || echo 4)
TAG="$(uname -s)-$(uname -m)-$(uname -r)"
OUT="$DIR/results/$TAG-$(date +%Y%m%d-%H%M%S).txt"
mkdir -p "$DIR/results"

{
	echo "# host $(uname -a)"
	echo "# model $(sysctl -n hw.model 2>/dev/null || echo unknown)"
	echo "# ncpu $NC date $(date)"

	echo "== scenario=idle"
	./wakeup
	./spawn

	echo "== scenario=cpuload make-j-equivalent spinners=$NC"
	i=0
	while [ $i -lt $NC ]; do
		sh -c 'while :; do :; done' &
		i=$((i + 1))
	done
	sleep 1
	LOAD_PIDS=$(jobs -p)
	./wakeup
	./spawn
	for p in $LOAD_PIDS; do
		kill $p 2>/dev/null || true
	done
	wait 2>/dev/null || true

	if [ "${RUN_NICELOAD:-1}" = "1" ]; then
		echo "== scenario=niceload spinners=$NC nice=19"
		i=0
		while [ $i -lt $NC ]; do
			nice -n 19 sh -c 'while :; do :; done' &
			i=$((i + 1))
		done
		sleep 1
		LOAD_PIDS=$(jobs -p)
		./wakeup
		./spawn
		for p in $LOAD_PIDS; do
			kill $p 2>/dev/null || true
		done
		wait 2>/dev/null || true
	fi

	echo "== scenario=iostall seconds=20"
	./iostall . 20

	if [ "${RUN_AUDIO:-1}" = "1" ] && [ -c /dev/audio0 ]; then
		echo "== scenario=audio"
		sh "$DIR/audio.sh" 30 || true
	fi

	echo "== done"
} | tee "$OUT"

echo
echo "results written to $OUT"
