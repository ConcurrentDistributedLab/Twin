import argparse
import json
import os
import copy
import debug_logger
from debug_logger import dbg, dbg_dict
from data_loader import load_data_as_dataframe, parse_key_value_list, parse_marker_list
from plot_engine import render_plot
from file_utils import get_unique_base_dir, ensure_subdirectories

def load_json_config(path):
    dbg(f"Loading JSON config: '{path}'")
    try:
        with open(path, 'r') as f:
            data = json.load(f)
        dbg(f"Config loaded OK. Top-level keys: {list(data.keys())}")
        return data
    except Exception as e:
        print(f"[ERROR] Could not load config {path}: {e}")
        return {}

def recursive_replace(obj, replacements):
    """Recursively replaces {KEY} placeholders in config.

    If a string is exactly ``{KEY}``, the raw replacement value is returned
    (so generator values can inject lists/bools, e.g. exclude arrays).
    Otherwise placeholders are string-substituted.
    """
    if isinstance(obj, str):
        for key, val in replacements.items():
            if obj == f"{{{key}}}":
                dbg(f"recursive_replace: exact '{obj}' -> raw value {val!r}")
                return val
        new_str = obj
        for key, val in replacements.items():
            placeholder = f"{{{key}}}"
            if placeholder in new_str:
                dbg(f"recursive_replace: '{placeholder}' -> '{val}' in '{obj}'")
                new_str = new_str.replace(placeholder, str(val))
        return new_str
    elif isinstance(obj, dict):
        return {k: recursive_replace(v, replacements) for k, v in obj.items()}
    elif isinstance(obj, list):
        return [recursive_replace(i, replacements) for i in obj]
    else:
        return obj

def expand_plots(plots_def):
    """Expands generator templates into multiple plot configs."""
    expanded_plots = []
    for plot in plots_def:
        if 'generator' in plot:
            gen = plot['generator']
            keys = gen.get('keys', [])
            values_list = gen.get('values', [])
            template = copy.deepcopy(plot)
            del template['generator']

            print(f"[GEN] Expanding plot template into {len(values_list)} variants...")
            dbg(f"Generator keys={keys}, values_list ({len(values_list)} entries)={values_list}")

            for values in values_list:
                replacements = dict(zip(keys, values))
                dbg(f"Applying replacements: {replacements}")
                new_plot = recursive_replace(template, replacements)
                expanded_plots.append(new_plot)
        else:
            expanded_plots.append(plot)
    dbg(f"expand_plots: total plots after expansion = {len(expanded_plots)}")
    return expanded_plots

def resolve_exclude_names(exclude_list, rename_map):
    """
    Expand a plot-level exclude list so either raw program names (e.g. FRX_L)
    or display names (e.g. Twin-List) match the post-rename algorithm column.
    """
    names = set(exclude_list or [])
    if not names:
        return names
    for raw, display in (rename_map or {}).items():
        if raw in names:
            names.add(display)
        if display in names:
            names.add(raw)
    return names

def apply_plot_exclude(df, plot_def, rename_map):
    """Filter algorithms excluded by the plot definition in bash/config.json."""
    exclude_list = plot_def.get('exclude') or []
    if not exclude_list or df is None or df.empty:
        return df
    names = resolve_exclude_names(exclude_list, rename_map)
    dbg(f"Per-plot exclude names: {sorted(names)}")
    filtered = df[~df['algorithm'].isin(names)].copy()
    dbg(f"Per-plot exclude removed {len(df) - len(filtered)} rows; remaining={len(filtered)}")
    return filtered

def main():
    parser = argparse.ArgumentParser(description="Universal Benchmark Plotter")
    parser.add_argument('--config-file', required=True, help="Path to global config.json (styles)")
    parser.add_argument('--root-dir', required=True, help="Root directory to search for plot_config.json")
    parser.add_argument('--debug', action='store_true', help="Enable verbose debug output")

    args = parser.parse_args()

    # Enable debug mode globally
    debug_logger.set_debug(args.debug)
    if args.debug:
        print("[DEBUG] Debug mode ENABLED")

    # 1. Load Global Config
    print(f"[INIT] Loading global config: {args.config_file}")
    global_config = load_json_config(args.config_file)

    global_filters = global_config.get('filters', {"threads": {"all_threads": "threads > 0"}})
    print(f"[INIT] Loaded filter categories: {list(global_filters.keys())}")
    dbg_dict(global_filters, "Global filters")

    rename_map = parse_key_value_list(global_config.get('rename', []), separator='==')
    global_styles = {
        'exclude': global_config.get('exclude', []),
        'rename': global_config.get('rename', []),
        'order': global_config.get('order', []),
        'marker_slide_pts': global_config.get('marker_slide_pts', 0.0),
        'color_map': parse_key_value_list(global_config.get('color', []), separator=':'),
        'marker_map': parse_marker_list(global_config.get('marker', [])),
        'show_title': global_config.get('show_title', False),
        'show_legend': global_config.get('show_legend', True),
        'normalization': global_config.get('normalization', 'linear')
    }
    dbg_dict(global_styles, "Global styles")

    found_configs = False

    # 2. Walk through directory
    for root, dirs, files in os.walk(args.root_dir):
        if 'plot_config.json' in files:
            found_configs = True
            config_path = os.path.join(root, 'plot_config.json')
            print(f"\n[FOUND] Experiment config at: {config_path}")

            experiment_config = load_json_config(config_path)

            raw_plots_def = experiment_config.get('plots', [])
            if not raw_plots_def:
                print("[WARN] No plots defined in this config.")
                continue

            # Expand Generators
            plots_def = expand_plots(raw_plots_def)
            dbg(f"Total plots to render in '{root}': {len(plots_def)}")

            # Create unique base result directory
            res_dir = get_unique_base_dir(root, "res")
            os.makedirs(res_dir, exist_ok=True)
            print(f"[DIRS] Output directory created: {res_dir}")

            loaded_dfs = {}

            # --- Plot Loop ---
            for plot_idx, plot_def in enumerate(plots_def):
                input_filename = plot_def.get('input_file')
                out_filename = plot_def.get('output_file', 'plot.png')
                dbg(f"Plot {plot_idx + 1}/{len(plots_def)}: input_file='{input_filename}', output_file='{out_filename}'")

                if not input_filename:
                    dbg("No 'input_file' in plot_def, skipping.")
                    continue

                input_path = os.path.join(root, input_filename)

                # Load Data (Cached)
                if input_path not in loaded_dfs:
                    print(f"[DATA] Loading {input_filename}...")
                    df = load_data_as_dataframe(input_path, global_styles)
                    loaded_dfs[input_path] = df
                    dbg(f"DataFrame cached for '{input_path}'")
                else:
                    dbg(f"Using cached DataFrame for '{input_path}'")

                full_df = loaded_dfs[input_path]
                if full_df is None or full_df.empty:
                    dbg(f"DataFrame for '{input_path}' is None or empty, skipping plot.")
                    continue

                # --- Dynamic Filter Selection Logic ---
                x_def = plot_def.get('data_source', {}).get('x_value', 'threads')
                filter_tag = plot_def.get('filter_tag', plot_def.get('x_label', x_def).lower())

                if isinstance(filter_tag, str) and filter_tag.startswith("expression:"):
                    filter_tag = filter_tag.replace("expression:", "").strip()

                dbg(f"filter_tag resolved to: '{filter_tag}'")
                filters_def = global_filters.get(filter_tag)

                if not filters_def:
                    dbg(f"No filters found for tag '{filter_tag}', using 'all' (no filter).")
                    filters_def = {"all": None}

                # --- Iterate through Filters ---
                for filter_name, query_string in filters_def.items():
                    dbg(f"Applying filter '{filter_name}': query='{query_string}'")
                    try:
                        if query_string:
                            filtered_df = full_df.query(query_string)
                        else:
                            filtered_df = full_df.copy()
                    except Exception as e:
                        print(f"[WARN] Filter '{filter_name}' ({query_string}) failed: {e}")
                        dbg(f"Filter exception: {e}")
                        continue

                    if filtered_df.empty:
                        dbg(f"Filter '{filter_name}' resulted in empty DataFrame, skipping.")
                        continue

                    # Per-plot algorithm exclude from bash/config.json plot defs
                    filtered_df = apply_plot_exclude(filtered_df, plot_def, rename_map)
                    if filtered_df.empty:
                        dbg(f"Per-plot exclude emptied DataFrame for '{out_filename}', skipping.")
                        continue

                    dbg(f"Filter '{filter_name}' retained {len(filtered_df)} rows.")

                    # Determine Output Path
                    p_type = plot_def.get('type', 'stats')
                    aggregator = plot_def.get('data_source', {}).get('aggregator', 'mean')
                    dbg(f"plot_type='{p_type}', aggregator='{aggregator}'")

                    final_output_dir = ensure_subdirectories(res_dir, filter_name, p_type, aggregator)

                    # Render Plot (output filename comes from plot_def / bash/config.json)
                    render_plot(filtered_df, plot_def, global_styles, output_dir=final_output_dir)

    if not found_configs:
        print("[INFO] No 'plot_config.json' files found in the directory tree.")

if __name__ == "__main__":
    main()
