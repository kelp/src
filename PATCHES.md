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
| (pending) | 73cfd5a74e5 | (fill in) | first baseline run |

Baseline numbers to capture per machine:

| Metric | idle p99 | cpuload p99 | iostall max | audio mismatch |
|--------|----------|-------------|-------------|----------------|
| wakeup_ns   | | | n/a | n/a |
| spawn_us    | | | n/a | n/a |
| iostall_us  | n/a | n/a | | n/a |

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
