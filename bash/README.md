# Experiment scripts

JSON-driven runners for the Twin microbenchmarks. Prefer the repo-root workflow (`./install.sh`, `./run.sh`); this file covers only `bash/main.sh` and `bash/config.json`.

## Usage

From the repository root:

```bash
./bash/main.sh help
./bash/main.sh list -c bash/config.json
./bash/main.sh bash -c bash/config.json      # throughput runs
./bash/main.sh counts -c bash/config.json    # stats / counters runs
./bash/main.sh all -c bash/config.json       # both modes (default)
```

Options:

| Option | Meaning |
| --- | --- |
| `-c FILE` | Config file (default: `bash/config.json`) |
| `-i INDEX` | Config selection: `filtered` (default; only configs whose `run_in_modes` include the mode), `all`, or a 0-based index |

## Configuration

`config.json` has:

- `global_settings` - defaults shared by every experiment (`num_runs`, `cores`, `max_threads`, `threads`, `op_between`, `time_main`, `time_secondary`, `pinning`, `numa`)
- `configurations` - one entry per paper workload (`inserts/increase`, `rand/0`, ...)

Per-configuration fields (override globals when set):

| Field | Role |
| --- | --- |
| `name` | Results directory under `results/<name>/` |
| `programs` | Binaries to run (names without `.out`) |
| `threads` / `op_counts` / `op_percentages` / `del_percentages` | Workload sweep |
| `test_cases` | `{num, name, plots...}` entries passed as `-i` |
| `run_in_modes` | Which modes run this config (`bash`, `counts`) |
| `count_stats_flags` | Compile-time `-D...` flags for counts mode |
| `less_op` | Passed through as `-l` |

Validate JSON with `jq . bash/config.json`.

Optional configs in this directory:

- `config_main.json` -- recommended figures for `./run.sh` (default)

## Modes

- **bash** - builds without stats macros; writes throughput JSONL (`stats_*.jsonl`)
- **counts** - builds with `count_stats_flags`; writes counter JSONL (`counts_*.jsonl`)

Each run also writes a `plot_config.json` next to those files for `generatePlots.sh`.

## Output

```
results/<configuration_name>/
  stats_*.jsonl
  counts_*.jsonl
  plot_config.json
```

Reruns that would collide get a numeric suffix (`results/<name>_1/`, ...).
