# Running the experimental analysis (reviewers)

Follow these steps to reproduce the paper experiments on our evaluation machine.

## Step 1: Request access

Send us your SSH public key(s) and the IP address(es) from which you will connect. We will create an account on the machine and reply with a username and password for you to use.

## Step 2: Log in to the gateway

```bash
ssh [username]@thegates.ics.forth.gr
```

Replace `[username]` with the account we provide.

## Step 3: Reach the evaluation host

From the gateway, connect to the experiment machine:

```bash
ssh titan
```

## Step 4: Clone the repository

On `titan`, download the artifact:

```bash
git clone https://github.com/ConcurrentDistributedLab/Twin
```

## Step 5: Enter the repository

```bash
cd Twin
```

## Step 6: Make all shell scripts executable

```bash
find . -name "*.sh" -exec chmod +x {} +
```

## Step 7: Start a tmux session

Start a `tmux` session so the run survives disconnects:

```bash
tmux new -s twin
```

## Step 8: Create and activate the Python environment

System packages are already available on the evaluation machine. Create the project virtual environment, install Python dependencies, and activate it:

```bash
python3 -m venv .venv
source .venv/bin/activate
python -m pip install --upgrade pip
python -m pip install -r requirements.txt
```

## Step 9: Run the quick test

This is a short environment sanity check (not the full paper suite):

```bash
./run_quicktest.sh
```

On success it prints a summary with `N passed, 0 failed` and exits with status `0`.

## Step 10: Run the full experiments

From the repository root:

```bash
./run.sh
```

This executes the configured benchmarks and generates the plots.

Wait about 2.5 hours for the full suite to finish before inspecting outputs.

## Checking the results

Generated plots appear under `results/<configuration_name>/res/all_threads/<stats|counts>/mean/` (for example `results/rand/8/res/all_threads/stats/mean/5a.png`). Reference plots from the tested hardware live in the matching trees under `expectedResults/` (for example `expectedResults/rand/8/5a.png`). Plot filenames such as `2a.png`, `4b.png`, and `5a.png` follow the figure order in the paper (Figure 2a, 4b, 5a, …). Compare curve shapes and relative algorithm ordering against `expectedResults/` and the paper figures; absolute throughput values may vary slightly across runs.
