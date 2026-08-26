#!/bin/sh
# audio.sh - count playback glitches under CPU load.
#
# Plays a known sine stream through sndiod and captures the
# monitor mix at the same time. Any underrun shows up as a
# difference between played and captured samples.
#
# Requires sndiod running with monitor mode enabled:
#   rcctl set sndiod flags "-m play,rec,mon"
#   rcctl restart sndiod
#
# Override the sub-device with AUDIO_DEV (default snd/default).
# Set AUDIO_NOLOAD=1 to skip the CPU load phase.

set -u

DUR="${1:-30}"
DIR="$(dirname "$0")"
DEV="${AUDIO_DEV:-snd/default}"
TONE="$DIR/tone.bin"
CAP="$DIR/capture.raw"

[ -x "$DIR/tone" ] || cc -O2 -o "$DIR/tone" "$DIR/tone.c" -lm

"$DIR/tone" "$TONE" "$DUR" || exit 1
BYTES=$(wc -c < "$TONE" | tr -d ' ')

aucat -f "$DEV" -h raw -r 44100 -c 0:1 -e s16 -i "$TONE" &
PLAY_PID=$!

aucat -f "$DEV" -h raw -r 44100 -c 0:1 -e s16 -o "$CAP" &
CAP_PID=$!

if [ "${AUDIO_NOLOAD:-0}" != "1" ]; then
	NC=$(sysctl -n hw.ncpu 2>/dev/null || echo 4)
	i=0
	while [ $i -lt $NC ]; do
		sh -c 'while :; do :; done' &
		i=$((i + 1))
	done
	LOAD_PIDS=""
	for p in $(jobs -p); do
		case " $LOAD_PIDS " in
		*" $p "*) ;;
		*) LOAD_PIDS="$LOAD_PIDS $p" ;;
		esac
	done
fi

wait $PLAY_PID
sleep "$DUR"
kill $CAP_PID 2>/dev/null
for p in $LOAD_PIDS; do
	kill $p 2>/dev/null
done

CAPBYTES=$(wc -c < "$CAP" | tr -d ' ')
N=$((BYTES < CAPBYTES ? BYTES : CAPBYTES))
if [ "$N" -eq 0 ]; then
	echo "audio result=no_capture dev=$DEV"
	echo "enable monitor mode: rcctl set sndiod flags" \
	    "'-m play,rec,mon' && rcctl restart sndiod"
	rm -f "$TONE" "$CAP"
	exit 2
fi

DIFF=$(cmp -l -n "$N" "$TONE" "$CAP" | wc -l | tr -d ' ')
echo "audio dur=${DUR}s dev=$DEV mismatch_bp=$((1000000 * DIFF / N))" \
    "bytes=$N"

rm -f "$TONE" "$CAP"
[ "$DIFF" -eq 0 ]
