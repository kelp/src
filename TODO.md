# TODO

Format: `[ ]` open, `[x]` done (with result). See AGENTS.md for
how-to-work and PATCHES.md for the full experiment ledger; every
checked item below has a pass number and a commit there.

## Done

- [x] Phase 0 rig: fork github.com/kelp/src (branch perf), bench
  harness (wakeup/spawn/iostall/tone + run.sh), pilot VM harness,
  stock baseline. Result: loaded spawn p99 = 100ms, exactly the
  roundrobin_period signature - the project's first target.
- [x] hz sweep (option HZ 250/500/1000): spawn p99 tail 40/20/12ms
  vs 100ms stock, medians worsen. Result: tail scales with
  10*hardclock; raising hz is a blunt trade, not a fix.
- [x] Loop 1 mechanism (passes 1.1-1.5): nice(-1) discriminator,
  ktrace gap analysis, dt runqlat histogram, per-tid counts.
  Result: FIFO-tail quantum mechanism - a task that loses its cpu
  re-enqueues at the FIFO tail and waits a full quantum; spinners
  absorb the bulk, children are the felt p99.
- [x] Stale-tree bug found and fixed (pass 1.6): stage_build_named
  only extracted sys.tgz when /usr/src/sys was missing, so wp1,
  lp1, q20, rrv, rrdt all compiled pre-patch sources - EXP-A,
  EXP-C, exp3 "nulls" were stock kernels with unused defines.
  Harness now re-extracts unconditionally and verifies.
- [x] RR quantum 20ms verified on a fresh tree (passes 1.7-1.8):
  loaded spawn p99 100ms -> 20-21us class (4.7x), no bulk
  regression; wait histogram moved 64-128ms -> 16-32ms buckets
  exactly as the mechanism predicted.
- [x] EXP-A WAKE_PREEMPT verified (pass 1.9): p99 100ms -> 2.4ms
  (42x), loaded p50 +47%, idle unchanged. EXP-C LIVE_PRI null
  confirmed honestly.
- [x] Combination runs (pass 2.0): WPLP and WPRR rejected -
  enqueue-side patches do not stack; a ~0.1% ~100ms p999 class
  survives every variant.
- [x] Carried patch 1 (ab1dacab4f7): WAKE_PREEMPT is default
  behavior in sys/kern/kern_sched.c; carried table updated.
- [x] Residual attribution attempt (pass 2.2): heavy stallers are
  NOT the spinners (tid+100000 mapping); @bigc[comm] map lost to
  the fetch window; btrace overhead inflates traced tails (61ms
  vs 2.4ms untraced) - attribution valid, magnitudes not.
- [x] Signing: repo-local user.signingkey=~/.ssh/id_ed25519_signing
  and gpg.ssh.program=/usr/bin/ssh-keygen; no agent involved.

## Open

- [ ] Residual hunt (Loop 1): [passes 2.3-2.5] the ~0.5% residual
  class is child-exit wait4 gaps quantized to k*100ms; enqueue-side
  resched does not gate it (stale-eq carried patch keeps p99
  1.8ms; live-eq added in wplive leaves p999 at 102ms). The stall
  likely originates child-side or in the exit path (reaper). Next:
  child-side ktrace (-i inherited into children, mfs file,
  kdump -R with the gap in field $3, awk '$3+0>0.05') and find
  which child syscall or dispatch carries the k*100ms gap; check
  the reaper's wakeup path too. Also retry the softclockmp
  priority check (write ps to a file, grep, short lines).
- [ ] Wakeup-floor family (b), ULE-style interactivity credits:
  sleep/runtime ratio decays estcpu continuously, giving woken
  tasks rank advantage. Design against ../refs/freebsd-src
  sched_ule.c and ../refs/linux fair.c - shapes only, never copy.
  Target: the residual class and the ~80ms wakeup-pair max.
- [ ] Wakeup-pair residual: cpuload wakeup max ~80ms on rr20
  (100ms stock) - a second stall source on the futex-ping-pong
  path; dt-trace it.
- [ ] Loop 2 writeback: iostall max 39.8ms -> 1.9ms under rr20
  (pass 1.8, unexplained). Attribute the stall path
  (bufdaemon/cleaner/vfs), design smoothing if it holds.
- [ ] Loop 3 wakeup floor: idle wakeup p50 ~4.5us - IPL/doorbell
  cost floor. Low priority unless audio demands it.
- [ ] RRQUANTUM_NS decision: keep as config option (today) or
  drop; note upstream-ability - WAKE_PREEMPT changes
  equals-never-preempt-equals semantics (upstream-risky, fine for
  the fork).
- [ ] Audio: tone.c/audio.sh untested in the VM (no audio device);
  first audio numbers must come from hardware.
- [ ] Bare metal M1 (arm64) bring-up: port the harness, repeat
  cpuload/niceload, check whether the mechanism and carried patch
  hold off-x86.
- [ ] Optional tooling: btrace keyed-hist / multi-END support as a
  carried usr.sbin/btrace patch if future traces need it.
