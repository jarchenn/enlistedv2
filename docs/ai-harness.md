# AI tuning harness (W6b, AI_SPEC 12.2–12.5)

The harness measures the bot AI against the AI_SPEC §12 thresholds. It reads the AI's own counters (`AiMetrics`, `StuckEvent`, `AiProf` in `src/ai_core.h`) and never changes AI behaviour.

- Code: `src/selftest_ai_harness.cpp` (stages 2900+), declared in `src/ai_harness.h`.
- Thresholds: `content/ai/test_thresholds.json`.
- Runner: `tools/run_ai_harness.sh`.

## Running it

The whole set on a list of maps:

```bash
tools/run_ai_harness.sh <build dir> --maps "village random omaha montcastre" --minutes 20 --bots 32
```

- **course** (§12.2) runs on `--course-maps`, default `course`.
- **rebuild** (§12.4) runs on the course plus every soak map.
- **soak** (§12.3 / §12.5) runs on every map in `--maps`.

The script exits 1 if any run misses a `fail` threshold.

- For a smoke test, use `--maps village --minutes 2`.
- To run one suite only, add `--suites soak` (or `course`, `rebuild`).

A single run:

```bash
NormandyMapmaker.exe --ai-harness soak --map random:7:560 --minutes 20 --bots 32 --difficulty regular
```

Options:

| Option | Meaning |
|---|---|
| `--map a,b` | `village`, `course`, `random[:seed[:size]]`, a map name in `content/examples` or `maps/`, or a `.nmap.json` path. |
| `--minutes f` | Soak length in match minutes. Matches restart until the time is used up. |
| `--bots n` | Combatants (default 32). For a soak, 62.5 % attack; the local player takes one defender slot and stands still. |
| `--attackers n`, `--respawns n` | Override the scenario's numbers. |
| `--difficulty` | `recruit`, `regular` or `veteran` (default: `rules.json`). |
| `--seed`, `--size`, `--objectives`, `--structures` | Random map generator settings. The size is capped by the Blank Slate build area (580 m). |
| `--save-map` | Writes the generated map to `maps/ai_random_<seed>.nmap.json`. |
| `--full-think` | Disables thinking LOD (bots far from humans think at full rate). The course and rebuild runs always use it. |
| `--thresholds file` | Use another thresholds file. |
| `--baseline file` | Use another baseline file. |
| `--save-baseline file` | Writes this soak in the baseline format. |
| `--tag s` | Suffix for the report names. |

## Outputs

All outputs go to `<build>/selftest_out/`.

- **`aiharness_<suite>_<map>.json`** holds the full result. It contains:
  - every metric
  - a per-minute timeline (stuck events, stuck events per bot, attacker deaths, frame and AI ms, living attackers)
  - the bots with the most stuck events
  - objective fall times (`captured` or `held`, in seconds from the phase start)
  - frame, AI main-thread and match-step time statistics (mean, p50, p95, p99, max)
  - the baseline comparison
  - every stuck event, with map, match, bot, role, start time, duration, position, state, resolving ladder step and reason
  - the verdicts
- **`aiharness_<suite>_<map>.txt`** is a short text report: verdicts, objective falls, per-minute table, stuck events.
- **`report_aiharness_<suite>_<map>.txt`** is the usual self-test report. Each miss is a `FAIL` line.
- **`aiharness_summary.txt`** is written by the runner script: one line per run, plus the misses.
- **`ai_stuck.log`** is written by the AI itself: one line per stuck event.

## Thresholds

Each entry names a metric of the result JSON by its dotted path, for example `frameMs.p95`, and gives a bound:

- `lt` (<), `max` (<=), `gt` (>) or `min` (>=)
- `"jog"` stands for the jog speed from `movement.json`

`severity` can be:

- `fail` (the default)
- `warn` (reported, never fails)
- `off`

`maps.<tag>.<suite>` overrides entries for one map. The tag is the map file name without `.nmap.json`, for example `omaha`, or one of `village`, `course`, `random`.

- `objectiveFall_s`: each objective must fall between `min` and `max` seconds after its phase starts. A phase held past `max` is a miss. A phase still running at the end of a shorter run gives no verdict.
- `baseline.compare`: bounds as a ratio of the saved baseline, `docs/baseline/ai_baseline.json` (§2.2, the old grid AI). The default asks for attacker deaths per bot per minute ≤ 0.75 × the baseline ([DECISION]: "clearly" means at least 25 % fewer).

How some metrics are counted:

- **Stuck events per bot** divide by every AI bot, attackers and defenders.
- **Open stuck events at the end of a run:**
  - If the bot is still alive, the event counts toward `stuckOver5s` once it is longer than 5 s.
  - If the bot died while stuck, the event is listed as `abandoned (bot died)`.
- **Frame time** is wall-clock time, rendering included. The first 30 frames of each match are skipped, because they include the navmesh bake.

## Notes

- The W6 suites stay W6's regression gate: `--selftest-ai-course` (the course and the live rebuild on the course) and `--soak-ai`. The harness adds:
  - thresholds in a data file
  - any map, including the large random map
  - per-event stuck logs in the result
  - objective fall times and frame time
  - the baseline verdict
  - an exit code for tuning runs
- Omaha and Mont-Castre are passed by their map names once they exist (`--maps "omaha montcastre"`). Give per-map overrides in `maps` if needed.
