import os
from debug_logger import dbg

def get_unique_base_dir(base_path, folder_name="res"):
    """
    Finds a unique directory name by appending a number if needed.
    """
    dbg(f"get_unique_base_dir called: base_path='{base_path}', folder_name='{folder_name}'")
    full_path = os.path.join(base_path, folder_name)

    if not os.path.exists(full_path):
        dbg(f"Path '{full_path}' is available, returning as-is.")
        return full_path

    counter = 1
    while True:
        new_name = f"{folder_name}_{counter}"
        full_path = os.path.join(base_path, new_name)
        dbg(f"Path exists, trying '{full_path}' (counter={counter})")
        if not os.path.exists(full_path):
            dbg(f"Found unique path: '{full_path}'")
            return full_path
        counter += 1

def ensure_subdirectories(base_res_dir, filter_name, plot_type, aggregator="mean"):
    """
    Creates structure: res/{filter_name}/{type}/{aggregator}
    Example: res/threads_gt3/stats/mean
    """
    dbg(
        f"ensure_subdirectories: base='{base_res_dir}', "
        f"filter='{filter_name}', type='{plot_type}', agg='{aggregator}'"
    )

    # Normalize inputs
    f_name = filter_name if filter_name else "all"
    p_type = plot_type.lower() if plot_type else "stats"
    agg_sub = aggregator.lower() if aggregator else "mean"

    # Construct path
    target_path = os.path.join(base_res_dir, f_name, p_type, agg_sub)
    dbg(f"Target path resolved: '{target_path}'")

    os.makedirs(target_path, exist_ok=True)
    dbg(f"Directory ensured (created or already exists): '{target_path}'")

    return target_path
