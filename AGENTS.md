# AGENTS.md

Guidance for agents working in this repo (OpenBSD interactive
performance fork, branch perf). PATCHES.md is the ledger: every
claim there carries a pass number and a commit. TODO.md is the
queue. Read both before doing anything.

## What this is

We find and patch interactive latency problems in OpenBSD,
measured - never argued. The bench VM is the arbiter. Loop 1
(scheduler tail latency) produced carried patch 1; Loops 2
(writeback) and 3 (wakeup floor) are queued.

## Layout

- bench/            micro-benchmarks (wakeup, spawn, iostall,
                    tone) + run.sh scenarios
- PATCHES.md        ledger: passes, measurements, verdicts,
                    carried-patch table
- sys/              the kernel; carried patches live unguarded
- ../vm/pilot.py    pexpect serial-console VM harness
- ../vm/results/    every suite run archived as text
- ../refs/{freebsd-src,linux}  design reference ONLY, never copy

## VM sessions (pilot.py)

- Run inline: `uv run --with pexpect python - <<'PYEOF'` with
  workdir ../vm; `import pilot`; call pilot.preflight() first.
- pilot.sh(c, cmd) and pilot.shr(c, cmd) (root) return (rc, out);
  rc comes from a token appended to your command.
- End with pilot.halt(c) on the live session. NEVER
  halt(boot_disk()) after a reboot_inplace - deadlock.
- Guest logins: bench/benchbench; root via `su root` + rootbench.
- Parked kernels: /home/bench/kernel-<name>; /bsd.stock = stock.
  select_kernel(c, path) + reboot_inplace(c) to switch.

## Serial console traps

- The console wraps at 80 columns. Keep every sh() command under
  ~78 chars or the rc token wraps and expect() times out.
- The rc token must land at line start: never pipe output without
  a trailing newline (`pgrep | tr '\n' ' '` glues the token to
  the output and hangs the session).
- out always contains the echoed command - never assert on
  out.strip() == "" for file contents; use `test -s file`.
- /tmp is wiped on every reboot; /etc/rc.securelevel (sets
  securelevel -1, required for kern.allowdt=1) and /home/bench
  persist.

## Kernel variant builds - the verification chain

1. Transfer sys.tgz, md5-check in the guest.
2. Extraction is UNCONDITIONAL (pass 1.6: a stale /usr/src/sys
   turned three experiments into guaranteed nulls). Never build
   against a pre-existing guest tree.
3. Verify the guest SOURCE carries the hook: grep -c HOOK
   /usr/src/sys/kern/<file> >= expected count.
4. After the build, verify the ARTIFACT: grep for a marker string
   in the kernel binary (add a boot printf to create one). A -D
   flag on the compile line proves nothing; the source must
   consume it.
5. Boot gate: `uname -v | grep -q TAG && echo Y || echo N` (tag =
   variant name uppercased).

dmesg may lose early-boot printfs before msgbuf capture - binary
grep is the reliable check.

## btrace quirks (usr.sbin/btrace on the guest)

- ONE rule per probe type; duplicates die with "Cannot register
  multiple probes". Merge guards into if() inside the action.
- ONE END block; multi-statement inside it is fine.
- Keyed hist() does not parse: use unkeyed hist + keyed
  max()/count().
- Map reads auto-create zero-valued keys (noise in dumps).
- Nested-if map inserts can silently no-op: use filter guards
  (/@ts[tid]/) for outer guards, if() only for thresholds.
- comm builtin works (dtev_comm). Map keys are tid+100000
  (THREAD_PID_OFFSET) for arg0 and tid alike.
- Start btrace via a guest runner script with nohup: printf the
  launch line to /tmp/runner, then `pilot.shr(c, "sh /tmp/runner")`
  - login shells SIGHUP background children on exit.
- Startup check: `test -s /tmp/rl.log` (errors land there), then
  pgrep -x btrace.
- btrace overhead inflates tails (p99 2.4ms -> 61ms traced):
  attribution stays valid, magnitudes do not.
- Fetch the WHOLE log (cat or tail -n 400) BEFORE any reboot;
  /tmp dies with the guest. Pass 2.2 lost its comm map to a
  120-line window.

## Measurement discipline

- Paired runs only: fresh stock-ctrl suite + variant suite in the
  same session (pilot.run_variant_pair(c, name, tag)).
- Verdict by ledger rule: every pass gets numbers in PATCHES.md,
  a commit, then the next move. Nulls are results - but only
  after the stale-tree check above.
- run.sh cpuload uses 8 spinners; dt sessions have used 4 via
  /tmp/sp. Say which one you ran.
- Guest clock check: sysctl kern.clockrate (confirm hz) and
  `uname -v` (confirm variant) before every measurement.

## Git

- Branch perf; master tracks upstream, never commit to it.
- Signing is repo-local: user.signingkey=
  ~/.ssh/id_ed25519_signing, gpg.ssh.program=/usr/bin/ssh-keygen.
  Do NOT restore the global git-ssh-sign wrapper for this repo -
  it probes a dangling agent socket and can hang commits.
- Commit style: first line = what changed (<=50 chars), body =
  why. No emojis.

## Method

Measure, ledger, commit. Falsify fast; a fix that measures
nothing dies with the same discipline as a mechanism. Trust
internal code, distrust build state - every silent-failure mode
found so far lived in the pipeline, not the kernel.
