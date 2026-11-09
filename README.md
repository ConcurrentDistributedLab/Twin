# Twin: Concurrent Double-Ended Priority Queues

This artifact builds and evaluates **Twin** together with several baseline algorithms under a shared benchmark setup.

**Zenodo:** [https://doi.org/10.5281/zenodo.24056204](https://doi.org/10.5281/zenodo.24056204)

The following algorithms are evaluated:

- **Twin-List** (`Twin_List`) -- Twin with a list-based backbone [[1]](#references)
- **Twin-LJ** (`Twin_LJ`) -- Twin built on the Linden-Jonsson priority queue [[1], [2]](#references)
- **Twin-TSL** (`Twin_Tsl`) -- Twin built on TSLQueue [[1], [3]](#references)
- **LJ** (`LJ`) -- Linden-Jonsson skiplist priority queue [[2]](#references) ([original code](https://github.com/jonatanlinden/PR); see [Third-party code and licenses](#third-party-code-and-licenses))
- **TSL** (`Tsl`) -- TSLQueue lock-free priority queue [[3]](#references) ([original code](https://github.com/dcs-chalmers/TSLQueue); see [Third-party code and licenses](#third-party-code-and-licenses))
- **BST** (`Bst`) -- non-blocking binary search tree of Ellen et al. [[4]](#references) ([setbench / implementations](https://bitbucket.org/trbot86/implementations/src/master/cpp/debra/); see [Third-party code and licenses](#third-party-code-and-licenses))
- **CC-BST** (`CCBst`) -- BST with CC-Synch combining [[4], [5]](#references)
- **Chromatic** (`Chromatic`) -- Chromatic tree of Brown, Ellen, and Ruppert [[6]](#references) ([original code](https://bitbucket.org/trbot86/implementations/src/master/cpp/debra/); see [Third-party code and licenses](#third-party-code-and-licenses))
- **CC-Chromatic** (`CCChromatic`) -- Chromatic tree with CC-Synch combining [[5], [6]](#references)

All algorithms have been adapted to use the same allocator in `Library/Allocator/` (`BumpAllocator`). It is a per-thread arena / bump allocator: each thread owns cacheline-aligned slabs (currently 4 MiB), allocates by advancing a bump pointer inside its current slab, never frees individual objects, and only reclaims memory in bulk when the allocator is destroyed at the end of an execution.

## Repository structure (high level)

- `Implementations/` -- Twin and baseline implementations
  - `Implementations/Twin/Twin_List/` -- Twin-List
  - `Implementations/Twin/Twin_LJ/` -- Twin-LJ
  - `Implementations/Twin/Twin_Tsl/` -- Twin-TSL
  - `Implementations/LJ/` -- LJ baseline
  - `Implementations/Tsl/` -- TSL baseline
  - `Implementations/Bst/` -- BST and CC-BST
  - `Implementations/Chromatic/` -- Chromatic and CC-Chromatic
- `src/` -- shared benchmark code (thread spawn, workloads, result tags)
- `Library/` -- pinning, RNG, timers, bump allocator
- `bash/` -- JSON-driven experiment scripts (`bash/config.json`, `bash/main.sh`)
- `plotGen/` -- Python plotter (matplotlib / numpy / pandas)
- `expectedResults/` -- reference plots from the tested hardware (see below)
- `install.sh` -- bootstrap a blank Ubuntu/Debian machine and project `.venv`
- `run_quicktest.sh` -- environment sanity check (not the full paper suite)
- `run.sh` -- full benchmark run (optional config path; default `bash/config_main.json`), then plot generation
- `generatePlots.sh` -- plot all experiments found under a results root (default: `results/`)
- `config.json` -- global plot style (algorithm names, colors, markers, order)
- `Makefile` -- builds all algorithm binaries as `*.out` in the repo root
- [`LICENSE.md`](LICENSE.md) -- GPL-3.0 for original Twin material (see [License](#license))
- [`reviewers.md`](reviewers.md) -- short step-by-step guide for running the experiments on our evaluation machine

Experiment script details (modes, config fields, output layout): [`bash/README.md`](bash/README.md).

If you are a reviewer evaluating the artifact on our machine, follow [`reviewers.md`](reviewers.md) for access, login, `tmux`, the quick test, and the full experiment run. The rest of this README covers building and running the artifact on any suitable host.

## Getting started

### Preparing the host machine

Tested on **Ubuntu 24.04.4 LTS** (kernel `6.8.0-106-generic`, `x86_64`). From the repository root:

```bash
./install.sh
```

This installs system packages and creates a project-local Python environment at `.venv` with packages from `requirements.txt`.

Packages installed by `install.sh`:

```
build-essential g++ make libnuma-dev python3 python3-pip python3-venv jq bc numactl
```

Python packages (into `.venv`):

```
matplotlib numpy pandas
```

Versions observed on the tested Ubuntu 24.04 machine (for reference; exact patch levels may differ):

```
build-essential        12.10ubuntu1
g++                    4:13.2.0-7ubuntu1
make                   4.3-4.1build2
libnuma-dev            2.0.18-1ubuntu0.24.04.1
python3                3.12.3-0ubuntu2.1
python3-pip            24.0+dfsg-1ubuntu1.3
jq                     1.7.1-3ubuntu0.24.04.2
bc                     1.07.1-3ubuntu4
```

Activate the venv whenever you run plotting tools manually:

```bash
source .venv/bin/activate
```

#### Manual install (if you cannot use `install.sh`)

```bash
sudo apt-get update
sudo apt-get install -y build-essential g++ make libnuma-dev python3 python3-pip python3-venv jq bc numactl
python3 -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
```

## Quick test

The quick test is a **sanity check only**. It verifies tools, the Python `.venv`, a smoke compile of `Twin_List.out`, and a help-output run. It does **not** reproduce the full experiment suite.

```bash
./run_quicktest.sh
```

### Expected output

On success, the script prints a summary of the form:

```
############################################
Quick test summary: N passed, 0 failed
############################################
[PASS] Environment is ready for work.
   Activate Python: source .venv/bin/activate
   Full build:      make all
   Benchmarks:      cd bash && ./main.sh help
```

and exits with status `0`.

### Acceptable variation

- Absolute paths printed for tools may differ by machine.
- Compiler warning noise during the smoke build is acceptable as long as `Twin_List.out` is produced and the help check passes.
- Any `[FAIL]` line means the environment is not ready; fix the reported issue (usually by re-running `./install.sh`) before full experiments.

## Build

```bash
make
# or
make all
```

Binaries are placed in the repository root as `*.out` (for example `Twin_List.out`, `LJ.out`, `Chromatic.out`).

Show CLI options for any binary:

```bash
./Twin_List.out -h
```

Options:

```
  -t <threads>    Set number of threads (default: 10)
  -T <threads>    Set maximum number of threads (default: 10)
  -c <cores>      Set available cores (default: 4)
  -p <percent>    Set percentage of operations (insert/delete) (default: 50)
  -e <percent>    Set percentage of extractions (deletemin/deletemax) (default: 50)
  -i <test_id>    Run specific test (default: run all tests)
  -o <op_count>   Set operation count (default: 32768)
  -l <less_op>    Set less operations threshold (default: 32)
  -b <op_between> Set operations between checks (default: 500)
  -m <ms>         Set time limit for main time operations (default: 5000 ms)
  -s <ms>         Set time limit for secondary time operations (default: 5000 ms)
  -h              Show this help message
```

Example single-binary run:

```bash
./LJ.out -t 16 -T 96 -i 0
```

## Running the full experiments

`bash/config_main.json` (the default for `./run.sh`) encodes the recommended paper workloads for Figures 2a, 3a, 4a, 4b, and 5a. `bash/config.json` encodes the complete experiment suite. On the tested machine both use:

- `cores` / `max_threads`: 96
- `threads`: 1, 4, 8, 16, 32, 48, 96
- `num_runs`: 10
- `pinning`: `titan` (NUMA-aware pinning for the machine described below)
- configurations: `inserts/increase`, `inserts/random`, `extracts/50`, `extracts/0`, `rand/0`, `rand/8`, `rand/14`, `rand/P60`

From the repository root, after a successful quick test:

```bash
./run.sh
# or with a custom experiment config:
./run.sh path/to/config.json
```

What `run.sh` does:

1. Activates `.venv` (fails if missing; run `./install.sh` first).
2. Runs `./bash/main.sh -c <config>` (default `bash/config_main.json`; builds as needed and executes configured experiments).
3. Runs `./generatePlots.sh`, which finds every `plot_config.json` under `results/` and renders plots.

Equivalent manual steps:

```bash
source .venv/bin/activate
./bash/main.sh -c bash/config_main.json
./generatePlots.sh
```

Useful experiment commands:

```bash
./bash/main.sh list -c bash/config_main.json
./bash/main.sh help
./bash/main.sh bash -c bash/config_main.json
./bash/main.sh counts -c bash/config_main.json
./bash/main.sh all -c bash/config_main.json
```

### Expected outputs

Raw results and plot configs are written under:

```
results/<configuration_name>/
```

Examples:

```
results/inserts/increase/
results/inserts/random/
results/extracts/50/
results/extracts/0/
results/rand/0/
results/rand/8/
results/rand/14/
results/rand/P60/
```

Each experiment folder contains JSONL measurement files (for example `stats_*.jsonl`, `counts_*.jsonl`) and a `plot_config.json`.

Generated plots are written under each experiment folder as:

```
results/<configuration_name>/res/all_threads/<stats|counts>/mean/<plot_name>.png
```

Plot filenames match the names under `expectedResults/` (for example `2a.png`, `4b.png`, `5a.png`). Those names follow the figure order in the paper (Figure 2a, 3a, ...). Reference plots from the tested hardware mirror `bash/config.json` configuration names:

```
expectedResults/inserts/increase/<plot_name>.png
expectedResults/inserts/random/<plot_name>.png
expectedResults/extracts/0/<plot_name>.png
expectedResults/extracts/50/<plot_name>.png
expectedResults/rand/0/<plot_name>.png
expectedResults/rand/8/<plot_name>.png
expectedResults/rand/14/<plot_name>.png
expectedResults/rand/P60/<plot_name>.png
expectedResults/legend.png
```

### Acceptable variation

- Absolute throughput and counter values will differ across machines, OS versions, background load, and thermal/frequency state.
- Compare shape and relative ordering of curves against `expectedResults/`, not exact Y-axis values.
- On hardware other than the 96-core machine below, edit `bash/config.json` (`cores`, `max_threads`, `threads`, and `pinning`) before claiming a reproduction of the paper configuration.
- Re-running `generatePlots.sh` may create `res_1/`, `res_2/`, ... if a previous `res/` directory already exists; compare the newest `res*` tree.

## Plotting only

If results already exist:

```bash
source .venv/bin/activate
./generatePlots.sh results
# or
python plotGen/main.py --root-dir results/<configuration_name> --config-file config.json
```

## Tested hardware

Experiments and the plots in `expectedResults/` were produced on a machine with the following characteristics:

```
Architecture:                x86_64
CPU op-mode(s):              32-bit, 64-bit
Byte Order:                  Little Endian
CPU(s):                      96
On-line CPU(s) list:         0-95
Vendor ID:                   GenuineIntel
Model name:                  Intel(R) Xeon(R) Gold 5318Y CPU @ 2.10GHz
Thread(s) per core:          2
Core(s) per socket:          24
Socket(s):                   2
CPU max MHz:                 2100.0000
CPU min MHz:                 800.0000
Caches (sum of all):
  L1d:                       2.3 MiB (48 instances)
  L1i:                       1.5 MiB (48 instances)
  L2:                        60 MiB (48 instances)
  L3:                        72 MiB (2 instances)
NUMA node(s):                4
  NUMA node0 CPU(s):         0-11,48-59
  NUMA node1 CPU(s):         12-23,60-71
  NUMA node2 CPU(s):         24-35,72-83
  NUMA node3 CPU(s):         36-47,84-95

Distro:  Ubuntu 24.04.4 LTS
Kernel:  6.8.0-106-generic
Arch:    x86_64
```

Memory on the evaluation machine (from `free -h`):

```
Mem:   495Gi total
Swap:  8.0Gi total
```

### Thread pinning

`bash/config.json` sets `"pinning": "titan"` for this topology. That selects `-DPIN_TITAN` at build time (see `bash/common.sh`) and uses the mapping in `Library/Pinning/pinning.c`.

With `PIN_TITAN`, threads are pinned in a round-robin fashion across the two sockets and, within each socket, across its NUMA nodes (and then onto hyperthreads once physical cores are filled). This matches Titan's 2-socket / 4-NUMA layout above.

Without `PIN_TITAN` (any other `"pinning"` value, or leaving the flag unset), threads are pinned with a simple round-robin over CPU ids `[0, max_cores)`.

To port pinning to another machine:

1. Edit `Library/Pinning/pinning.c` and add a machine-specific mapping (or change the default round-robin policy).
2. Optionally wire a new compile flag the same way `PIN_TITAN` is wired in `bash/common.sh` / the `Makefile` `PINNING` variable.
3. Set `"pinning"` in `bash/config.json` (and related configs) to the mode you want, and adjust `cores` / `max_threads` / `threads` to match the host.

Runs also used Transparent Huge Pages (THP) in `always` mode. On this x86_64 platform THP is 4 MiB per page.

## Supported claims

The artifact supports the paper's main performance claims through the throughput plots for Figures 2a, 3a, 4a, 4b, and 5a (plot filenames such as `2a.png` match that figure order). Recommended observations to check against `expectedResults/` and the paper:

- **Figure 2a (highly biased insertions):** Twin-LJ achieves the best performance among the evaluated DEPQs, as skip lists avoid tree traversal and rebalancing overheads.
- **Figure 3a (random insertions):** Twin-TSL achieves performance comparable to other tree-based DEPQs despite maintaining two trees and performing two insert operations, thanks to its low synchronization overhead and improved locality.
- **Figures 4a and 4b (extract-only):** Twin-based algorithms built on state-of-the-art single-ended priority queues provide better performance for extract-intensive workloads, and Twin implementations outperform all other DEPQs.
- **Figure 5a (mixed workloads):** Twin-LJ achieves the highest throughput beyond a certain number of threads, while both Twin-LJ and Twin-TSL outperform the other baselines. Twin algorithms that perform well under extraction-heavy workloads maintain their advantage in mixed workloads. An exception is Twin-List, whose high insertion cost significantly degrades its overall performance.

To reproduce the complete set of experiments from the paper and its supplementary material, run `./run.sh bash/config.json` (or `./bash/main.sh -c bash/config.json`).

## System runtime libraries

Binaries need the usual system shared libraries from the link step:

- `libpthread`, `libm`, `libnuma`, `librt`
- `libstdc++`, `libgcc_s`, `libc`, `ld-linux-x86-64.so.2`

## License

Original Twin material in this repository -- the Twin technique [[1]](#references), Twin-List, the shared benchmark under `src/` and `Library/`, and the experiment / plotting tooling under `bash/` and `plotGen/` -- is released under the **GNU General Public License v3.0 (GPL-3.0)**. The full text is in [`LICENSE.md`](LICENSE.md).

Third-party baselines bundled in this repository remain under their own licenses (see below). Redistributing the artifact must retain those notices and license files as well.

## Third-party code and licenses

Several baselines are derived from publicly available original implementations. Copyright notices and license texts in those trees must be retained.

| Component | Location | Origin / notes | Upstream | License as present in this tree |
| --- | --- | --- | --- | --- |
| LJ | `Implementations/LJ/` (also adapted in `Implementations/Twin/Twin_LJ/`) | Lock-free priority queue by Linden and Jonsson [[2]](#references); adapted from Keir Fraser's skiplist | [jonatanlinden/PR](https://github.com/jonatanlinden/PR) | BSD-style notice in `Implementations/LJ/prioq.c`; full text in `Implementations/LJ/LICENSE.md` |
| TSL | `Implementations/Tsl/` (also adapted in `Implementations/Twin/Twin_Tsl/`) | TSLQueue by Rukundo and Tsigas [[3]](#references) | [dcs-chalmers/TSLQueue](https://github.com/dcs-chalmers/TSLQueue) | **AGPL-3.0**; full text in `Implementations/Tsl/LICENSE.md` (also governs the Twin-TSL adaptation) |
| BST / CC-BST | `Implementations/Bst/` | Non-blocking BST of Ellen et al. [[4]](#references); CC-BST additionally uses CC-Synch [[5]](#references). Via ASCYLIB / setbench lineage | [trbot86/implementations (debra)](https://bitbucket.org/trbot86/implementations/src/master/cpp/debra/); ASCYLIB GPL-2 text from [LPD-EPFL/ASCYLIB](https://github.com/LPD-EPFL/ASCYLIB) | GPL version 2 (headers in `ellen_impl.h`); full text in `Implementations/Bst/LICENSE.md` |
| Chromatic / CC-Chromatic | `Implementations/Chromatic/` | Chromatic tree of Brown, Ellen, and Ruppert [[6]](#references); CC-Chromatic additionally uses CC-Synch [[5]](#references) | [trbot86/implementations (debra)](https://bitbucket.org/trbot86/implementations/src/master/cpp/debra/) | GPL version 3; full text in `Implementations/Chromatic/LICENSE.md` |
| CC-Synch library | `Implementations/*/CCSynchLibrary/` (used by Twin and CC-* variants) | CC-Synch combining of Fatourou and Kallimanis [[5]](#references); headers credit Nikolaos D. Kallimanis | [nkallima/Synch-Framework](https://github.com/nkallima/Synch-Framework) | LGPL-2.1; full text in each `CCSynchLibrary/LICENSE.md` |

If you redistribute this repository, keep the third-party notices above with the corresponding code, and ship the full license texts required by each license (especially GPL / AGPL / LGPL components).

## References

1. Panagiota Fatourou, Eric Ruppert, and Ioannis Xiradakis. Concurrent Double-Ended Priority Queues. *CoRR*, abs/2508.13399, 2025. DOI: [10.48550/ARXIV.2508.13399](https://doi.org/10.48550/ARXIV.2508.13399). URL: [https://arxiv.org/abs/2508.13399](https://arxiv.org/abs/2508.13399).

2. Jonatan Linden and Bengt Jonsson. A Skiplist-Based Concurrent Priority Queue with Minimal Memory Contention. In *Proc. 17th International Conference on Principles of Distributed Systems*, volume 8304 of LNCS, pages 206-220. Springer, 2013. DOI: [10.1007/978-3-319-03850-6_15](https://doi.org/10.1007/978-3-319-03850-6_15).

3. Adones Rukundo and Philippas Tsigas. TSLQueue: An Efficient Lock-Free Design for Priority Queues. In *Euro-Par 2021: Parallel Processing - Proc. 27th International Conference on Parallel and Distributed Computing*, volume 12820 of LNCS, pages 385-401. Springer, 2021. DOI: [10.1007/978-3-030-85665-6_24](https://doi.org/10.1007/978-3-030-85665-6_24).

4. Faith Ellen, Panagiota Fatourou, Eric Ruppert, and Franck van Breugel. Non-blocking Binary Search Trees. In *Proc. 29th ACM Symposium on Principles of Distributed Computing*, pages 131-140, 2010. DOI: [10.1145/1835698.1835736](https://doi.org/10.1145/1835698.1835736).

5. Panagiota Fatourou and Nikolaos D. Kallimanis. Revisiting the combining synchronization technique. *ACM SIGPLAN Notices*, 47(8):257-266, February 2012. DOI: [10.1145/2370036.2145849](https://doi.org/10.1145/2370036.2145849).

6. Trevor Brown, Faith Ellen, and Eric Ruppert. A General Technique for Non-blocking Trees. In *Proc. 19th ACM Symposium on Principles and Practice of Parallel Programming*, pages 329-342, 2014. DOI: [10.1145/2555243.2555267](https://doi.org/10.1145/2555243.2555267).
