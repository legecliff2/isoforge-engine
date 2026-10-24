# encounter_runner

Headless, deterministic encounter scenarios. The program loads a `scenario@1` file and the pack it names, replaces the board's spawns with the roster, and runs `sim::NearestEnemyPolicy` on every creature until the fight ends.

```
encounter_runner [--log] [--root DIR] <scenario.json>
```

`--root` is the repository root used to find `content/packs` (default: the current directory).

Exit 0 when the encounter ends, 1 on a load or run error (`encounter_runner: ` on stderr), 2 on usage. A successful run writes nothing to stderr.

Stdout without `--log` is the checked-in golden. The lines, in order, are `scenario`, `seed`, `end_condition`, `round_cap`, `outcome`, `round`, `ended`, `teams_down`, `pcs`, `hp`, `surprised`, `digest` (`Combat::hash` as 16 lowercase hex), and `commands`. `--log` appends `log <count>` and one line per sim event. The same seed and pack contents always print the same bytes.

Each `scenarios/<name>/` directory holds `scenario.json` and `golden.txt`. `ctest` test `encounter_scenario_<name>` runs the program twice and compares stdout to the golden, then runs `--log` twice and checks that the log starts with that report.

The end policy is `sim::ScenarioEnd` (`set_scenario_end`): the built-in kind, then a round cap. When `round_cap != 0` and `state.round > round_cap`, and the built-in policy returned nothing, the outcome is `EndReason::Custom`. That value is replay input. `replay(..., surprised_at_start(), scenario_end())` ends on the same step. Omitting it replays under the team rule with no cap, because an end policy is not stored in the command log. `Combat::hash` does not fold `ScenarioEnd`. It folds the `EncounterEnded` event the policy emits (`a` = `EndReason`).

Schema and field rules: `schemas/scenario/v1.schema.json` and `docs/CONTENT_CONVENTIONS.md` section 24. `content_validate` checks scenarios from `tools/validate/validate_packs.py`. The C++ schema registry does not register `scenario@1`.
