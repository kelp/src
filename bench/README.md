bench/ interactive-latency harness
==================================

Measures the latency classes that decide desktop feel: wakeup,
process spawn, I/O stalls, and audio glitches. Runs on any
OpenBSD host with cc(1). No ports required.

Quick start (as root, on an idle machine):

    cd /usr/src/bench
    sh run.sh

Results land in bench/results/<tag>-<date>.txt. Run before and
after each kernel change, same machine, similar thermal state.
Record deltas in PATCHES.md.

Metrics
-------

wakeup   Pipe ping-pong between two threads. Time from write()
         to poll() wake plus read on the other side, reported
         as p50/p90/p99/p999/max in nanoseconds. This is the
         core interactivity number; it exposes scheduler and
         timer granularity problems directly.

spawn    fork + exec(/usr/bin/true) + wait cycle, percentiles
         in microseconds. Exposes kernel lock contention and
         exec path costs under load.

iostall  One thread bulk-writes a file while another probes
         with open+pread+close every 2ms. Probe latency
         percentiles expose writeback burst stalls.

audio    Plays a sine via aucat while capturing the monitor
         mix, then byte-compares. Mismatch counts glitches
         (underruns), reported in parts per billion. Needs
         sndiod monitor mode:

             rcctl set sndiod flags "-m play,rec,mon"
             rcctl restart sndiod

Scenarios
---------

idle      wakeup + spawn on a quiet machine.
cpuload   Same, with one spinner thread per CPU. This is the
          "desktop during make -j" proxy.
iostall   Bulk writer plus small-read prober for 20s.
audio     Glitch count during full-CPU load for 30s.

Load model
----------

CPU load is NCPU `sh -c 'while :; do :; done'` spinners, which
approximates a parallel build's scheduling pressure without
depending on a source tree or toolchain state. It saturates all
run queues the way make -j does.

Environment knobs
-----------------

RUN_AUDIO=0     skip the audio scenario
AUDIO_NOLOAD=1  audio.sh without spinners
AUDIO_DEV=...   sndio sub-device (default snd/default)

Notes by platform
-----------------

amd64    Primary development target today. Numbers from VMs are
         indicative only; use bare metal for patch decisions.

arm64    Target hardware is Apple Silicon (M1 MacBook Pro).
         Supported in-tree via the apl* driver family, display
         through apldrm. No GPU acceleration exists, so userland
         rendering is CPU-bound there regardless of our kernel
         work; expect wins in jitter, stalls, underruns, not
         graphics throughput. Install via m1n1 + U-Boot.

Method notes
------------

- Pin nothing, nice nothing: we measure what a default system
  does. Expect run-to-run variance up to ~5% on p50; treat only
  p99+ movements below ~15% as noise unless repeated.
- VMs share host timers; never compare VM numbers across hosts.
- For attribution beyond these numbers use profile(4) sampling
  and dt(4) probes on the running kernel.
