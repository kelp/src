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

## Loop 1: tail attribution (goal loop, serial)

Hypothesis v1: some per-tick-counted scheduling mechanism,
not the RR quantum, gates loaded spawn resumes.

### Pass 1.1: niced-spinners discriminator

Stock kernel, four scenarios back-to-back in one session:
idle, cpuload (equal-priority spinners), niceload (same
spinners at nice +19), iostall. Harness gained the niceload
scenario and a 1s settle before measuring (startup-fork
transients were polluting earlier samples).

Result: complete tail collapse under nice alone.

| scenario | wakeup max | spawn p99 |
|----------|-----------|-----------|
| cpuload  | 100.0ms   | 100.0ms   |
| niceload | 0.20ms    | 1.85ms    |

Family verdict: scheduling competition among EQUAL
priorities is required for the tail; serialization family
(lock holders, disk paths) cannot explain nice-sensitivity.
Estcpu/priority interplay is suspect #1 going forward.

Next pass (1.2): ktrace the spawn loop under equal-priority
load in-guest; kdump timings attribute the 100ms to concrete
switch/syscall records instead of theory. dt/profile remain
backup instruments if ktrace context-switch data is too
coarse.

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

### Loop 1 progress: pass 1.1 verdict + passes 1.2/A-exp

Pass 1.1 (discriminator): niced load kills the tail
completely (spawn p99 100ms -> 1.85ms). Equal-priority
competition required; serialization family excluded.

Pass 1.2 (ktrace attribution, 400 spawns under load): four
stalls >15ms summing 1.34s, ALL showing the same shape:
parent forks instantly, blocks in wait4, and the child's
FIRST trace record appears only 79-614ms later. The victim
is precisely the freshly forked child waiting for its first
dispatch; nothing kernel-side in the fork/wait paths is slow.

Nice(-1) discriminator: parent one notch above competitors ->
p99 collapses to 2.0ms (50x), residual stalls pushed into
p999 because the fixed nice delta erodes under accumulation.
Rank competition among equals is the gate; queue position
(FIFO tail) decides ties.

EXP-A (option WAKE_PREEMPT: need_resched also on EQUAL
priority at setrunqueue): REFUTED as fix. Loaded spawn p99
91.5ms vs stock 100.0ms. Preempting equals does not rescue
tail position; FIFO ordering keeps latecomers behind peers
until rotation revisits them.

Model correction found while drafting EXP-B: fork ZEROES
p_estcpu (p_startzero region; sole assignment site is
sched_bsd.c:513 schedclock accrual). Children start
best-ranked by theory yet starve in practice -> live-rank
assumptions are wrong somewhere. Next instrument (pass 1.3):
sample ps -O pri,ni,ucomm frames inside the stall window to
read actual ranks of spinner vs bench tasks while starving,
then design EXP-C against measured values, not theory.

### Pass 1.3 frame capture + EXP-C

Frame caught mid-stall inside the guest:

  PID   PRI NI STAT  CPU COMM
  5123   52  0 Rp/0  24 sh      hog running on cpu0
  37831  50  0 R+/0  36 true    fresh child, BETTER rank

Child outranks the hog outright yet stays unserved. Also
notable: hog pri ~52-54 means saturated estcpu does NOT push
ranks to MAXPRI as classic 4BSD math would suggest; decay at
1Hz keeps mid-range values.

EXP-C (option LIVE_PRI: setrunqueue also compares against the
live on-cpu task's p_usrpri instead of possibly-stale
spc_curpriority): REFUTED. Loaded spawn p99 = 99961us vs
stock control 99900us; wakeup max even regressed on that
sample (284ms).

Interim synthesis after three refuted scheduler-side fixes:
rank advantage (nice -1) fully erases the tail, but three
different enqueue-time comparison/queueing patches do not.
Whatever delays the child is not decided by setrunqueue-time
comparisons. Spc_curpriority staleness and FIFO ties are real
quirks but not the gate. Next instrument is definitive rather
than speculative: dt(4) static sched tracepoints (enqueue,
on__cpu, off__cpu are compiled TRACEPOINTs in kern_sched.c)
captured around a stall window will show exactly which task
holds which cpu across the gap and what event finally frees
it.

### Pass 1.4: btrace/dt run-queue histogram (first dt data)

Getting dt running required: variant kernel with
`pseudo-device dt` (stock GENERIC lacks it), securelevel -1
via an /etc/rc.securelevel SCRIPT (it is sh'd, not read; a
value of 0 is overridden back to 1 by rc's default rule) and
`sysctl -w kern.allowdt=1` at securelevel < 1.

btrace quirks found: printf lacks %l modifiers (long values
must stay inside maps/hists), expressions cap at 5 operands,
interval:hz probes do not fire, END prints on SIGINT.

run-queue latency (enqueue -> on-cpu, microseconds) for a
full loaded spawn session, ~6M on-cpu events:

  [0,32us)      ~5.5M   healthy bulk
  [1K,2K)       17,839  unexplained 1-2ms mode (not tick)
  [64K,128K)     1,178  THE quantum-scale stall population
  [128K,+)         200+ tail to seconds (dequeue-churn
                          artifact suspected, see below)

The 64-128ms bucket is the smoking gun population: it
contains exactly the p99~100ms events the spawn benchmark
reports, now attributed to SCHEDULER RUN-QUEUE WAIT rather
than any syscall, lock, or disk path. The kernel genuinely
leaves these tasks runnable-but-unscheduled for 1-2 full
round-robin periods.

Next: per-tid big-bucket counts (@bigcnt[tid]) correlated
with static spinner tids from ps, to answer WHO starves:
fresh children (expected) or someone else (surprise).

### Pass 1.5: per-tid attribution — the FIFO-tail mechanism

@bigcnt[tid] over a loaded spawn session concentrates on a
handful of static tids, not thousands of children:

  ~4 tids with 123-396 stalls each  = the four spinners
  ~7 singleton tids (2-44 each)     = spawn children/misc

Reconciled with ktrace (children stall ~1% of spawns, each
a singleton tid) and the hist totals, ONE mechanism now
explains every observation in the loop:

  A task that loses its cpu and re-enqueues lands at the
  FIFO TAIL of its priority queue and waits a full quantum
  before its turn. Spinners are perpetually re-enqueued
  after preemption (constant 100ms waits); children hit the
  same wait occasionally (~1% = the benchmark's p99).

Consistency checks that all pass:
- nice(-1) rank advantage beats queue position (50x fix)
- three enqueue-side comparison fixes do nothing (they
  change when resched fires, not where the loser waits)
- exp3 RR-quantum decoupling failed because the quantum the
  loser waits for is set by WHOEVER RAN BEFORE it in queue
  order, not by roundrobin_period... consistent with the
  hz-sweep: tail == quantum length == 10*hardclock period
  regardless of RR timer cadence.

Candidate countermeasure families, in increasing scope:
  a) FIFO tail -> head rotation among equals at each
     quantum expiry (true round-robin between equals)
  b) shorter effective quanta for tasks that just yielded
     vs tasks that ran long (scheduling-class debt)
  c) interactivity credit per ULE: sleep/runtime ratio
     decays estcpu continuously, giving woken tasks rank
     advantage proportional to recent sleep

All three would be tested through the established pair
harness; each maps to a small, reviewable kern_sched.c /
sched_bsd.c change. Reference reading queued:
freebsd sched_ule.c (interactivity + pickcpu) and linux
fair.c (wakeup granularity) for design shapes only.

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

### Pass 1.6: RRDT pair runs clean - and exposes the stale-tree bug

Paired dt-traced sessions landed (RRDT rr=20ms vs dt1 stock, one boot
each, hz=100 both): spawn p99 99951 vs 100056 us; @h [64K,128K) 1140
vs 1131 events (window-normalized 16.1/s vs 12.7/s). Identical tails.

Then roundrobin() (sched_bsd.c) was read properly: it preempts the
current task within 2 RR fires whenever spc_nrun > 0. A REAL 20ms
quantum must have collapsed the tail. It did not, because the quantum
never reached the code: stage_build_named extracted sys.tgz only when
/usr/src/sys was MISSING (created at a1build, never refreshed), so
every variant since (wp1, lp1, q20, rrv, rrdt) compiled STALE
pre-patch sources; my options rode as -D flags on hookless code. The
compile-line flag proved the define was passed, not that code consumed
it. Consequences: EXP-A, EXP-C, and exp3 nulls are VOID - they
measured stock with unused defines. The dmesg rrquantum print was
absent because the printf never compiled in, not from msgbuf loss.

What stands: the hz sweep and dt1 (upstream config knobs), every
stock measurement, and the ktrace/dt attribution chain including the
pass 1.5 FIFO-tail finding.

Harness fixes: stage_build_named now re-extracts the source
unconditionally; btrace quirks recorded (one rule per probe type, one
END block, keyed hist unsupported, map reads auto-create zero keys,
nested-if map inserts can silently no-op - use filter guards); the
exp_rrdt gate typo RRVDT->RRDT fixed.

Re-verification queued: rebuild rrdt on a fresh tree with a
build-artifact check (grep rrquantum in the kernel binary), rerun the
pair; then rebuild + rerun EXP-A (WAKE_PREEMPT) and EXP-C (LIVE_PRI).
Supersedes the exp3 interpretation recorded in pass 1.5.

### Pass 1.7: quantum fix CONFIRMED - first real tail improvement

Fresh-tree rrdt rebuild with the full verification chain: guest
source carries RRQUANTUM_NS (grep x2), the binary carries the
rrquantum string (grep x1), kernel boots as RRDT#0. Paired run vs
dt1 stock control, hz=100, 4 spinners, spawn n=20000:

  spawn (us)   stock     rr=20ms
  p50          1118      1572
  p90          1573      19751
  p99         100052     21220    (4.7x better)
  p999        189669     153593
  max        1081002     952570

Run-queue wait histogram: [64K,128K) collapsed 1373 -> 153 (9x);
[16K,32K) grew 114 -> 3408 (30x). The wait mass moved from the
100ms quantum to the 20ms one - the pass 1.5 FIFO-tail mechanism
holds with roundrobin_period as the gate, matching roundrobin()'s
code (preempt within 2 fires when spc_nrun > 0).

Trade to note: the loaded bulk got lumpier (p90 1.6 -> 19.8 ms) -
re-enqueued equals now wait one 20ms slice instead of mostly
slipping in under 2ms. The bound is what improved: a 100ms spike
became a 20ms plateau, and p999/max improved too.

Next: full run.sh pair on rr20 (idle/cpuload/iostall/niceload) per
the established methodology; rebuild + rerun EXP-A/EXP-C to restore
honest falsification records; then the carried-patch decision
(quantum default vs configurable option).
