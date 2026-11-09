"""
debug_logger.py
Centralized debug logging for the benchmark plotter.
Enable via --debug CLI flag or DEBUG=1 env variable.
"""

import os
import inspect

_debug_enabled = False

def set_debug(enabled: bool):
    global _debug_enabled
    _debug_enabled = enabled

def is_debug() -> bool:
    return _debug_enabled or os.environ.get("DEBUG", "0") == "1"

def dbg(message: str, tag: str = "DEBUG"):
    """Print a debug message with caller context."""
    if not is_debug():
        return
    frame = inspect.stack()[1]
    caller = f"{os.path.basename(frame.filename)}:{frame.lineno}"
    print(f"[{tag}] ({caller}) {message}")

def dbg_dataframe(df, label: str = "DataFrame", max_rows: int = 5):
    """Print a summary of a DataFrame."""
    if not is_debug():
        return
    import pandas as pd
    print(f"\n[DEBUG] === {label} ===")
    print(f"  Shape   : {df.shape}")
    print(f"  Columns : {list(df.columns)}")
    if not df.empty:
        print(f"  Head({max_rows}):\n{df.head(max_rows).to_string(index=False)}")
    print(f"[DEBUG] === end {label} ===\n")

def dbg_dict(d: dict, label: str = "Dict"):
    """Print a dict nicely."""
    if not is_debug():
        return
    print(f"\n[DEBUG] === {label} ===")
    for k, v in d.items():
        print(f"  {k}: {v}")
    print(f"[DEBUG] === end {label} ===\n")
