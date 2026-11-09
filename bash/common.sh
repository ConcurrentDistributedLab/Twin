#!/bin/bash

# Shared experiment helpers (sourced by stats/counts modes).
# TEST_CASES=(
#     "0 test_insert_increase_time"
#     "1 test_insert_random_time"
#     "2 test_delete_rkeys"
#     "3 test_random_with_elements_rkeys_time"
# )
# PROGRAMS=("Twin_List" "Chromatic" "CCChromatic" "Bst" "CCBst" "LJ" "Twin_LJ" "Tsl" "Twin_Tsl")


# Get the directory where this script is located
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Default configuration file (do not overwrite if already set by main.sh -c/--config)
CONFIG_FILE="${CONFIG_FILE:-${SCRIPT_DIR}/config.json}"

# Configuration variables (will be loaded from JSON)
declare -g NUM_RUNS
declare -ga THREADS
declare -g MAX_THREADS
declare -g CORES
declare -ga OP_PERCENTAGES
declare -ga DEL_PERCENTAGES
declare -ga OP_COUNTS
declare -g LESS_OP
declare -ga OP_BETWEEN
declare -g TIME_MAIN
declare -g TIME_SECONDARY
declare -ga TEST_CASES
declare -ga PROGRAMS
declare -ga STATS_FLAGS
declare -g CONFIG_NAME
declare -g CURRENT_CONFIG_INDEX
declare -g PINNING_FLAG
declare -g SNUMA_SUPPORT

# Global array to store created folders (reset between configs)
declare -gA RESULT_FOLDERS

# Global array to track folders by config name within a single script execution.
# This persists across deinitialize_folders so that two configs with the same
# name (within the same run) share the same output folder.
# On a new script execution it starts empty, so a fresh folder is always created.
declare -gA CONFIG_NAME_FOLDERS

# Function to check if jq is installed
check_jq() {
    if ! command -v jq &> /dev/null; then
        echo "ERROR: jq is required but not installed."
        echo "Please install jq: sudo apt-get install jq"
        exit 1
    fi
}

# Function to load configuration from JSON
load_config_from_json() {
    local config_file="$1"
    local config_index="$2"
    
    check_jq
    
    if [ ! -f "$config_file" ]; then
        echo "ERROR: Configuration file '$config_file' not found!"
        exit 1
    fi
    
    echo "Loading configuration $config_index from $config_file..."
    
    # Check if configuration index exists
    local config_count=$(jq '.configurations | length' "$config_file")
    if [ "$config_index" -ge "$config_count" ]; then
        echo "ERROR: Configuration index $config_index not found. Available: 0-$((config_count-1))"
        exit 1
    fi
    
    # Store the index globally so we can extract the raw JSON later
    CURRENT_CONFIG_INDEX="$config_index"
    
    CONFIG_NAME=$(jq -r ".configurations[$config_index].name" "$config_file")
    echo "Using configuration: $CONFIG_NAME"
    
    # Load scalar values
    NUM_RUNS=$(jq -r ".configurations[$config_index].num_runs // .global_settings.num_runs" "$config_file")
    CORES=$(jq -r ".configurations[$config_index].cores // .global_settings.cores" "$config_file")
    MAX_THREADS=$(jq -r ".configurations[$config_index].max_threads // .global_settings.max_threads" "$config_file")
    LESS_OP=$(jq -r ".configurations[$config_index].less_op" "$config_file")
    OP_BETWEEN=$(jq -r ".configurations[$config_index].op_between // .global_settings.op_between" "$config_file")
    TIME_MAIN=$(jq -r ".configurations[$config_index].time_main // .global_settings.time_main" "$config_file")
    TIME_SECONDARY=$(jq -r ".configurations[$config_index].time_secondary // .global_settings.time_secondary" "$config_file")
    
    # Pinning mode from config (titan => -DPIN_TITAN)
    local pin_mode=$(jq -r ".configurations[$config_index].pinning // .global_settings.pinning // \"default\"" "$config_file")
    if [ "$pin_mode" == "titan" ]; then
        PINNING_FLAG="-DPIN_TITAN"
    else
        PINNING_FLAG=""
    fi

    # numa from config (passed to make as SNUMA_SUPPORT / NUMA_SUPPORT)
    SNUMA_SUPPORT=$(jq -r ".configurations[$config_index].numa // .global_settings.numa // false" "$config_file")
    
    # Load arrays
    mapfile -t THREADS < <(jq -r "(.configurations[$config_index].threads // .global_settings.threads)[]" "$config_file")
    
    mapfile -t PROGRAMS < <(jq -r ".configurations[$config_index].programs[]" "$config_file")
    mapfile -t OP_PERCENTAGES < <(jq -r ".configurations[$config_index].op_percentages[]" "$config_file")
    mapfile -t DEL_PERCENTAGES < <(jq -r ".configurations[$config_index].del_percentages[]" "$config_file")
    mapfile -t OP_COUNTS < <(jq -r ".configurations[$config_index].op_counts[]" "$config_file")
    
    # Load count_stats_flags (with default fallback for backward compatibility)
    STATS_FLAGS=()
    local stats_count=$(jq ".configurations[$config_index].count_stats_flags | length" "$config_file" 2>/dev/null)
    if [ "$stats_count" != "null" ] && [ "$stats_count" -gt 0 ]; then
        mapfile -t STATS_FLAGS < <(jq -r ".configurations[$config_index].count_stats_flags[]" "$config_file")
    else
        STATS_FLAGS=("COMPUTE_STATS")
    fi
    
    # Load test cases (special handling for objects)
    TEST_CASES=()
    local test_count=$(jq ".configurations[$config_index].test_cases | length" "$config_file")
    for ((i=0; i<test_count; i++)); do
        local test_num=$(jq -r ".configurations[$config_index].test_cases[$i].num" "$config_file")
        local test_name=$(jq -r ".configurations[$config_index].test_cases[$i].name" "$config_file")
        TEST_CASES+=("$test_num $test_name")
    done

    # Display loaded configuration
    echo "Configuration loaded successfully:"
    echo "  NAME: $CONFIG_NAME"
    echo "  PINNING MODE: $pin_mode (Flag: '$PINNING_FLAG')"
    echo "  NUMA: $SNUMA_SUPPORT"
    echo "  NUM_RUNS: $NUM_RUNS"
    echo "  CORES: $CORES"
    echo "  MAX_THREADS: $MAX_THREADS"
    echo "  THREADS: ${THREADS[*]}"
    echo "  OP_PERCENTAGES: ${OP_PERCENTAGES[*]}"
    echo "  DEL_PERCENTAGES: ${DEL_PERCENTAGES[*]}"
    echo "  OP_COUNTS: ${OP_COUNTS[*]}"
    echo "  LESS_OP: $LESS_OP"
    echo "  OP_BETWEEN: $OP_BETWEEN"
    echo "  TIME_MAIN: $TIME_MAIN"
    echo "  TIME_SECONDARY: $TIME_SECONDARY"
    echo "  STATS_FLAGS: ${STATS_FLAGS[*]}"
    echo "  PROGRAMS: ${#PROGRAMS[@]} programs"
    echo "  TEST_CASES: ${#TEST_CASES[@]} test cases"
    echo ""
}

# Function to build stats flags string for make
build_stats_flags_string() {
    local flags_str=""
    
    for flag in "${STATS_FLAGS[@]}"; do
        flags_str="${flags_str} -D${flag}"
    done
    
    echo "$flags_str"
}

# Function to get configurations for a specific mode
get_configs_for_mode() {
    local config_file="$1"
    local mode="$2"
    
    check_jq
    
    # Create a temporary array to store matching config indices
    local -a matching_configs=()
    local config_count=$(jq '.configurations | length' "$config_file")
    
    for ((i=0; i<config_count; i++)); do
        # Check if mode is specified for this config (backward compatible)
        local run_in_modes=$(jq -r ".configurations[$i].run_in_modes[]?" "$config_file" 2>/dev/null)
        
        # If no run_in_modes field exists or if mode is in the list, include this config
        if [ -z "$run_in_modes" ] || echo "$run_in_modes" | grep -q "$mode"; then
            matching_configs+=("$i")
        fi
    done
    
    echo "${matching_configs[@]}"
}

# Function to list available configurations
list_configs() {
    local config_file="$1"
    
    check_jq
    
    if [ ! -f "$config_file" ]; then
        echo "ERROR: Configuration file '$config_file' not found!"
        exit 1
    fi
    
    echo "Available configurations in $config_file:"
    echo ""
    
    local config_count=$(jq '.configurations | length' "$config_file")
    for ((i=0; i<config_count; i++)); do
        local config_name=$(jq -r ".configurations[$i].name" "$config_file")
        # Load run/threads with fallback to global for display
        local num_runs=$(jq -r ".configurations[$i].num_runs // .global_settings.num_runs" "$config_file")
        local threads_count=$(jq "(.configurations[$i].threads // .global_settings.threads) | length" "$config_file")
        local pin_mode=$(jq -r ".configurations[$i].pinning // .global_settings.pinning // \"default\"" "$config_file")
        local numa=$(jq -r ".configurations[$i].numa // .global_settings.numa // false" "$config_file")
        
        local programs_count=$(jq ".configurations[$i].programs | length" "$config_file")
        local count_stats_flags=$(jq -r ".configurations[$i].count_stats_flags[]?" "$config_file" 2>/dev/null | tr '\n' ' ')
        
        if [ -z "$count_stats_flags" ]; then
            count_stats_flags="(default: COMPUTE_STATS)"
        fi
        
        echo "  [$i] $config_name"
        echo "      Runs: $num_runs, Threads: $threads_count configs, Pinning: $pin_mode, NUMA: $numa"
        echo "      Programs: $programs_count, Stats flags: $count_stats_flags"
    done
    echo ""
}

# Initialize folders for all combinations
initialize_folders() {
    echo "Initializing result folders for config: $CONFIG_NAME..."
    
    for op_count in "${OP_COUNTS[@]}"; do
        for op_percentage in "${OP_PERCENTAGES[@]}"; do
            for del_percentage in "${DEL_PERCENTAGES[@]}"; do
                local key="${op_percentage}_${del_percentage}"
                # Compound key: config name + params - unique per (name, combination)
                local name_key="${CONFIG_NAME}_${key}"
                
                if [[ -n "${CONFIG_NAME_FOLDERS[$name_key]}" ]]; then
                    # Another config with the same name already ran in this execution.
                    # Reuse its folder so results are appended to the same JSONL files.
                    local folder="${CONFIG_NAME_FOLDERS[$name_key]}"
                    echo "Reusing existing folder for config '$CONFIG_NAME': $folder"
                else
                    # First time we see this (config name + params) in this execution.
                    # create_results_folders handles the _counter suffix for reruns.
                    local folder=$(create_results_folders "$op_percentage" "$del_percentage" "$CONFIG_NAME")
                    CONFIG_NAME_FOLDERS["$name_key"]="$folder"
                    echo "Created folder: $folder"
                fi
                
                RESULT_FOLDERS["$key"]="$folder"
            done
        done
    done
    
    echo "All folders initialized successfully"
}

# Deinitialize folders
deinitialize_folders() {
    echo "Reset RESULT_FOLDERS..."

    RESULT_FOLDERS=()

    echo "RESULT_FOLDERS reset successfully"
}

# Get folder for specific op_count, op_percentage and del_percentage
get_result_folder() {
    local op_count=$1
    local op_percentage=$2
    local del_percentage=$3
    local key="${op_percentage}_${del_percentage}"
    
    if [[ -z "${RESULT_FOLDERS[$key]}" ]]; then
        echo "Error: Folder for op_count=$op_count, op_percentage=$op_percentage, del_percentage=$del_percentage not found!"
        return 1
    fi
    
    echo "${RESULT_FOLDERS[$key]}"
}

# Function to save the specific JSON configuration to the LEAF folder
# Modified to flatten arrays to scalars for op_counts, op_percentages, del_percentages
# AND merge global settings so the saved file is self-contained
save_experiment_config() {
    local folder_path=$1
    local op_c=$2
    local op_p=$3
    local del_p=$4
    
    # We use the global CURRENT_CONFIG_INDEX and CONFIG_FILE
    # to extract exactly the configuration block we are running.
    local config_json_file="${folder_path}/plot_config.json"
    
    # Use jq to:
    # 1. Merge global_settings with the specific configuration (local overrides global)
    # 2. Overwrite op_counts, op_percentages, del_percentages with single values (scalars)
    # 3. Save to the new file
    jq -n --slurpfile full_config "$CONFIG_FILE" \
       --argjson idx "$CURRENT_CONFIG_INDEX" \
       --argjson opc "$op_c" \
       --argjson opp "$op_p" \
       --argjson delp "$del_p" \
       '($full_config[0].global_settings + $full_config[0].configurations[$idx]) | 
        .op_counts = $opc | 
        .op_percentages = $opp | 
        .del_percentages = $delp' > "$config_json_file"
}

# Create results folders
create_results_folders() {
    local op_percentage=$1
    local del_percentage=$2
    local config_name=$3
    
    # Structure: results/{ConfigName}
    # ConfigName may contain slashes (e.g. inserts/increase, extracts/0, rand/8)
    local base_folder="results/${config_name}"
    local counter=1
    local final_folder="${base_folder}"
    
    # Handle duplicate runs by appending _counter
    while [ -d "${final_folder}" ]; do
        final_folder="${base_folder}_${counter}"
        ((counter++))
    done
    
    mkdir -p "${final_folder}"
    
    # Save the specific configuration JSON file in the LEAF folder
    save_experiment_config "${final_folder}" "$op_count" "$op_percentage" "$del_percentage"
    
    echo "${final_folder}"
}

# Move statistics files
move_statistics_files() {
    local target_folder=$1
    local file_pattern=$2
    
    echo "Moving $file_pattern files to ${target_folder}"
    
    for file in $file_pattern; do
        if [ -f "$file" ]; then
            mv "$file" "$target_folder"/
            echo "Moved: $file"
        fi
    done
}


generate_plot_config() {
    local output_folder=$1
    local config_index=$2
    
    local plot_config_file="${output_folder}/plot_config.json"
       
    jq -n \
       --slurpfile full_config "$CONFIG_FILE" \
       --argjson idx "$config_index" \
       '{
         plots: [ 
           $full_config[0].configurations[$idx].test_cases[] | 
           (.plots // []) | 
           .[] 
         ]
       }' > "$plot_config_file"
       
    echo "Generated plot_config.json with $(jq '.plots | length' "$plot_config_file") plots."
}

# Append one metrics row to a JSONL results file.
append_to_jsonl() {
    local file=$1
    local test_name=$2
    local algorithm=$3
    local threads=$4
    local run=$5
    local op_count=$6
    local op_percentage=$7
    local del_percentage=$8
    local metrics_json=$9 # This must be a valid JSON object string e.g. {"time": 10, "misses": 5}
    
    # Construct the JSON line
    jq -n -c \
       --arg tn "$test_name" \
       --arg alg "$algorithm" \
       --argjson th "$threads" \
       --argjson r "$run" \
       --argjson pc "$op_count" \
       --argjson pp "$op_percentage" \
       --argjson dp "$del_percentage" \
       --argjson lo "$LESS_OP" \
       --argjson tm "$TIME_MAIN" \
       --argjson met "$metrics_json" \
       '{
         test_id: $tn,
         algorithm: $alg,
         threads: $th,
         run: $r,
         params: {
           op_count: $pc,
           op_percentage: $pp,
           del_percentage: $dp,
           less_operations: $lo,
           main_time: $tm
         },
         metrics: $met
       }' >> "$file"
}

# Run test case for all programs
run_test_case() {
    local test_num=$1
    local test_name=$2
    local op_percentage=$3
    local del_percentage=$4
    local op_count=$5
    local output_folder=$6
    local run_test_fn=$7

    # Just run the function. The function is responsible for naming the file correctly
    # e.g., "stats_${test_name}.json" inside output_folder.
    for program in "${PROGRAMS[@]}"; do
        $run_test_fn "$program" "$test_num" "$test_name" "$op_percentage" "$del_percentage" "$op_count" "$output_folder"
    done
}

# Drive all configured op_count / percentage / test_case combinations.
common_main() {
    local run_test_fn=$1
    
    if [[ ${#RESULT_FOLDERS[@]} -eq 0 ]]; then initialize_folders; fi

    for op_count in "${OP_COUNTS[@]}"; do
        echo -e "\n=== Running tests with operation count: $op_count ==="
        for op_percentage in "${OP_PERCENTAGES[@]}"; do
            echo -e "\n--- Running tests with op_percentage: $op_percentage% ---"
            for del_percentage in "${DEL_PERCENTAGES[@]}"; do
                echo -e "\n--- Running tests with del_percentage: $del_percentage% ---"
                
                local output_folder=$(get_result_folder "$op_count" "$op_percentage" "$del_percentage")
                
                # One plot_config.json per result folder (covers all test cases).
                generate_plot_config "$output_folder" "$CURRENT_CONFIG_INDEX"
                
                for test_case in "${TEST_CASES[@]}"; do
                    test_num=${test_case%% *}
                    test_name=${test_case#* }
                    
                    run_test_case "$test_num" "$test_name" "$op_percentage" "$del_percentage" "$op_count" "$output_folder" "$run_test_fn"
                done
            done
        done
    done
    echo -e "\nTests completed."
}