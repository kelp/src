# Carried patches ledger

This branch carries interactive-performance work on top of
upstream OpenBSD. Every patch lands here only with a measured
delta from the bench/ harness on real hardware.

Baseline: stock upstream master at the commit listed below.
Machine and scenario noted per table. Numbers are from
bench/results/ files; keep the raw file, reference its name.

## Baseline

| Date | Commit | Machine | Notes |
|------|--------|---------|-------|
| 2026-08-26 | 73cfd5a74e5 | qemu q35, 4 vcpu KVM host-pass-through | VM baseline; indicative only |

Baseline numbers to capture per machine:

| Metric | idle p50/p99 | cpuload p50/p99/max | iostall p99/max |
|--------|--------------|---------------------|-----------------|
| wakeup_ns   | 4463 / 5947 | 8766 / 13709 / 200014529 | n/a |
| spawn_us    | 1076 / 1859 | 1081 / 100013 / 384613 | n/a |
| iostall_us  | n/a | n/a | 1970 / 15033 |

## A/B measurements

### A1: option HZ=1000 (kernel config only, zero source changes)

Paired back-to-back runs in the VM (same session conditions,
kernel identity verified in-band via `uname -v` showing A1#0
and `sysctl kern.clockrate` showing tick=1000). Results file
suffixes -stock-ctrl- and -hz1000-, dated 20260826.

| metric (loaded) | stock hz=100 | HZ=1000 | delta |
|--------|--------|---------|-------|
| wakeup p50 ns    | 9979  | 8741   | better |
| wakeup p99 ns    | 13776 | 16293  | worse |
| wakeup max ns    | 200013460 | 10103465 | 20x fewer outliers |
| spawn p50 us     | 1096  | 9915   | much worse |
| spawn p99 us     | 99995 | 11779  | 8x better |
| spawn max us     | 423939 | 29592 | 14x better |
| iostall probes/20s | 996 | 4211 | probe loop 4x faster |
| iostall p99 us     | 1016 | 994   | flat |

Reading:

1. The 100ms-tail pathology largely disappears. Stock wakeup
   max lands on exact 100/200ms boundaries (roundrobin_period
   signatures); with HZ=1000 the period-derived quantum drops
   to 10ms and worst observed wakeup falls to 10.1ms.
2. Trade-off found: under full CPU load, spawn MEDIAN rises
   ~9x (1.1ms -> 9.9ms) while its tail collapses. The fast
   quantum interleaves each fork/exec/wait cycle with far more
   involuntary preemption. Fine-grained interrupt accounting
   cannot be ruled out as a contributor.
3. Idle behaviour is essentially unchanged (wakeup/spawn p50
   within noise); the hz increase costs nothing when quiet.
4. Timer-resolution effects dominate helper loops: bench
   usleep(2ms) cadence becomes real at hz=1000 (probes jump
   from ~1000 to ~4200 per 20s window).

Not yet proven on hardware; VM numbers are indicative. Next
variants to test against this baseline: selective preemption
thresholds and scheduler quantum tuning rather than pure clock
rate (A2), informed by these distributions.

Headline finding from the very first loaded run: under four
spinners, spawn p99 lands almost exactly on a 100ms boundary
and wakeup max hits 200ms. That is the roundrobin_period
signature (kern_clock.c hardclock_period * 10 at HZ=100), not
noise. Area A has a clear target.

Audio metric not yet exercised in the VM (no audio device
configured); first audio numbers must come from hardware.

## Carried patches

| # | Patch | Area | Measured delta | Status |
|---|-------|------|----------------|--------|
| - | none yet | - | - | - |

Areas: A timer/preemption, B writeback smoothing,
C userland quick wins, D scheduler math, E kernel lock.

Rules:

1. No patch enters this table without before/after rows from
   bench/run.sh on bare metal.
2. Revert beats rationalize: a regression reverts the patch.
3. Upstream cherry-picks that fix or supersede a carried patch
   replace it; note the upstream commit hash in the row.
