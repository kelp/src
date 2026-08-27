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

### A2: HZ sweep {250, 500} plus repeat runs

All kernels config-only (option HZ=n). Fresh reinstall before
the sweep so every number below comes from one clean session;
each variant ran back-to-back against a fresh stock control.
Kernel identity verified per-run in-band via uname -v showing
A1/A2A/A2B build tags.

Loaded-scenario results (idle numbers flat everywhere):

| kernel | tick | RR period | wakeup max | spawn p50 | spawn p99 |
|--------|------|-----------|------------|-----------|-----------|
| stock x4   | 10ms | 100ms | 100-200ms | ~1.1ms | **100ms exact** |
| A2A hz250  | 4ms  | 40ms  | 80ms      | 1.09ms | **40.3ms** |
| A2B hz500  | 2ms  | 20ms  | 40ms      | 1.70ms | **20.4ms** |
| A1 hz1000  | 1ms  | 10ms  | 10-20ms   | 9.94ms | 11.8ms |

(sample counts: stock n=4, others n=1 tonight plus the n>=5
earlier A1 replicas which agree within a few percent)

Two laws fall out of this table:

1. The loaded spawn TAIL sits exactly on roundrobin_period =
   hardclock_period * 10 at every tick rate. This is the
   single dominant latency mechanism in OpenBSD's desktop
   behaviour under load. It is tunable without touching tick
   rate only if clockintr can fire between ticks; today it is
   coupled 1:10 to hz.
2. The spawn MEDIAN explosion appears only at hz=1000
   (9x), not at 250/500, despite shorter quantums already
   existing there. Suspected mechanism: estcpu accumulates
   once per user-mode tick (schedclock) while its 1Hz decay
   is fixed, so dynamic priority churn grows with tick rate
   and slices fork/exec/wait cycles into sub-quantum pieces.
   Normalising the account/decay ratio would decouple these.

Harness variance established meanwhile: loaded spawn p99
across seven stock samples spans 99914-100015us (+-0.05%),
and hz1000 loaded p50 across six samples spans 9890-9958us
(+-0.35%). The suite's tails are deterministic enough that
single paired runs are decision-grade for mechanisms of this
size; medians need 2-3 repeats when deltas are <10%.

One anomaly logged for follow-up: one stock control showed
loaded wakeup p50 2.1us (best-ever) instead of ~9us,
suggesting the spinner start race inside run.sh occasionally
leaves CPUs partially idle during the wakeup loop.

Decision: carry no change yet from the A family. Pure hz
increases trade a median cliff for tail improvements; the
right first carried patch is the accounting normalisation
(A3) or a quantum/estcpu decoupling patch designed against
the two laws above. Next session drafts A3 against
sys/kern/sched_bsd.c schedclock() and schedcpu().

### Exp3: decoupled RR quantum (falsifies the simple story)

Patch: option RRQUANTUM_NS=20000000 sets roundrobin_period
directly at hz=100 (sys/kern/kern_clock.c, guarded, default
off). Tick-aligned (exactly 2 hardclock periods), so no
sub-tick timer capability is required.

Result: NULL. Loaded spawn p99 = 100014us, identical to
stock control (99414-100015us across samples). All other
metrics flat.

Conclusion: roundrobin_period is NOT the direct limiter of
the loaded spawn tail; the exact 10x-tick scaling seen in
the hz sweep must flow through some other 10-coupled
mechanism. Prime suspects requiring attribution:

1. estcpu/priority interplay at 1Hz decay: the hz sweep
   changes both tick rate AND total dynamic-priority churn;
   RR period may have been a correlated bystander all along.
2. Kernel-lock holder being descheduled on a 10-tick
   cadence somewhere else in the pipe (fork/exec/wait under
   four same-priority spinners).
3. lbolt/sleep-channel rounding artifacts around the
   waitpid wakeup path.

Next step is measurement, not another blind variant:
profile(4) sampling and dt syscall probes inside the VM
during the loaded-spawn scenario to see where the 100ms
actually goes. This doubles as the E-track reconnaissance
(ranking locked syscalls by real traffic).

kern_clock.c keeps the guard-patched hook (default behaviour
unchanged); a carried patch needs a proven mechanism first.

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
