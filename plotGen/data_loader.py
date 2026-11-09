import json
import pandas as pd
import os
from debug_logger import dbg, dbg_dataframe, dbg_dict

def parse_key_value_list(kv_list, separator=':'):
    """
    Helper to convert ["Key:Value", "A:B"] list to dict {'Key': 'Value', 'A': 'B'}
    Handles cases like "Old==New" if separator is '=='
    """
    result = {}
    if not kv_list:
        dbg(f"parse_key_value_list called with empty list (separator='{separator}')")
        return result
    for item in kv_list:
        if separator in item:
            key, val = item.split(separator, 1)
            result[key.strip()] = val.strip()
        else:
            dbg(f"Item '{item}' does not contain separator '{separator}', skipping.")
    dbg(f"parse_key_value_list result: {result}")
    return result

def parse_marker_list(marker_list):
    """
    Special parser for markers like "Alg:marker:fillstyle:linestyle" or shorter forms.
    Supports:
      "Alg:marker"                        -> fillstyle='full', linestyle='-'
      "Alg:marker:fillstyle"              -> linestyle='-'
      "Alg:marker:fillstyle:linestyle"    -> full control (e.g. '--', ':', '-.')
    Returns dict: {'Alg': {'marker': 'o', 'fillstyle': 'none', 'linestyle': '--'}}
    """
    result = {}
    if not marker_list:
        dbg("parse_marker_list called with empty list")
        return result
    for item in marker_list:
        parts = item.split(':')
        if len(parts) >= 2:
            alg = parts[0]
            marker = parts[1]
            fillstyle = parts[2] if len(parts) > 2 else 'full'
            linestyle = parts[3] if len(parts) > 3 else '-'
            result[alg] = {'marker': marker, 'fillstyle': fillstyle, 'linestyle': linestyle}
            dbg(f"Parsed marker entry: alg='{alg}', marker='{marker}', fillstyle='{fillstyle}', linestyle='{linestyle}'")
        else:
            dbg(f"Marker item '{item}' has insufficient parts (needs at least 2), skipping.")
    dbg_dict(result, "parse_marker_list result")
    return result

def load_data_as_dataframe(file_path, global_config):
    """
    Loads JSONL data into a flat Pandas DataFrame.
    Applies global exclusions and renames immediately.
    """
    dbg(f"load_data_as_dataframe called with file_path='{file_path}'")

    if not os.path.exists(file_path):
        print(f"[ERROR] Data file not found: {file_path}")
        return None

    data_buffer = []
    try:
        with open(file_path, 'r') as f:
            for line_num, line in enumerate(f, start=1):
                if line.strip():
                    parsed = json.loads(line)
                    data_buffer.append(parsed)
                    dbg(f"Line {line_num}: parsed OK -> keys={list(parsed.keys())}")
    except json.JSONDecodeError as e:
        print(f"[ERROR] JSON Decode Error in {file_path}: {e}")
        return None

    dbg(f"Total records loaded from file: {len(data_buffer)}")

    if not data_buffer:
        dbg("data_buffer is empty, returning empty DataFrame.")
        return pd.DataFrame()

    # Normalize: Flattens nested dicts (metrics.time, params.op_count)
    df = pd.json_normalize(data_buffer)
    dbg_dataframe(df, "After json_normalize")

    # 1. Apply Excludes
    exclude_list = global_config.get('exclude', [])
    dbg(f"Exclude list: {exclude_list}")
    if exclude_list:
        before = len(df)
        df = df[~df['algorithm'].isin(exclude_list)]
        dbg(f"Excluded {before - len(df)} rows. Remaining: {len(df)}")

    # 2. Apply Renames
    # Expecting format ["OldName==NewName", ...]
    rename_list = global_config.get('rename', [])
    dbg(f"Rename list: {rename_list}")
    rename_map = parse_key_value_list(rename_list, separator='==')

    if rename_map:
        dbg_dict(rename_map, "Rename map applied to 'algorithm' column")
        df['algorithm'] = df['algorithm'].replace(rename_map)

    dbg_dataframe(df, "Final DataFrame returned by load_data_as_dataframe")
    return df