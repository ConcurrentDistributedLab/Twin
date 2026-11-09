#!/bin/bash

# Experiment entrypoint (see bash/README.md). Requires jq; builds via make.

# Get the directory where this script is located
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Default mode (can be overridden by command line argument)
MODE="all"
CONFIG_FILE="${SCRIPT_DIR}/config.json"
CONFIG_INDEX="filtered"  # select configs whose mode matches; override with -i

# Function to show usage
show_usage() {
    echo "Usage: $0 [mode] [options]"
    echo ""
    echo "Modes:"
    echo "  bash     - Run only bash mode (performance tests)"
    echo "  counts   - Run only counts mode (statistics tests)" 
    echo "  all      - Run all modes (default)"
    echo "  list     - List available configurations"
    echo "  help     - Show this help message"
    echo ""
    echo "Options:"
    echo "  -c, --config FILE    - Specify configuration file (default: config.json)"
    echo "  -i, --index INDEX    - Specify configuration index or 'filtered' or 'all' (default: filtered)"
    echo "                         'filtered': Run only configurations marked for this mode"
    echo "                         'all': Run all configurations regardless of mode"
    echo "                         number: Run specific configuration index"
    echo ""
    echo "Examples:"
    echo "  $0 bash                          # Run bash mode with filtered-selected configs"
    echo "  $0 bash -i all                   # Run bash mode for all configurations"
    echo "  $0 all -c myconfig.json          # Run all modes with custom config file"
    echo "  $0 all -i filtered                   # Run all modes with their respective configs"
    echo "  $0 list -c myconfig.json         # List configurations in custom file"
}

# Parse command line arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        bash|counts|all|list|help)
            MODE="$1"
            shift
            ;;
        -c|--config)
            if [ -z "$2" ] || [[ "$2" == -* ]]; then
                echo "Error: -c/--config requires a file path"
                show_usage
                exit 1
            fi
            CONFIG_FILE="$2"
            # Resolve relative paths against the caller's working directory
            if [[ "$CONFIG_FILE" != /* ]]; then
                CONFIG_FILE="$(pwd)/$CONFIG_FILE"
            fi
            shift 2
            ;;
        -i|--index)
            CONFIG_INDEX="$2"
            shift 2
            ;;
        *)
            echo "Error: Unknown argument '$1'"
            show_usage
            exit 1
            ;;
    esac
done

# Show help and exit
if [ "$MODE" = "help" ]; then
    show_usage
    exit 0
fi

# Source the necessary functions
source "${SCRIPT_DIR}/common.sh"

# List configurations and exit
if [ "$MODE" = "list" ]; then
    list_configs "$CONFIG_FILE"
    
    # Also show which modes each config runs in
    echo "Mode assignments for each configuration:"
    echo ""
    
    CONFIG_COUNT=$(jq '.configurations | length' "$CONFIG_FILE")
    for ((i=0; i<CONFIG_COUNT; i++)); do
        CONFIG_NAME=$(jq -r ".configurations[$i].name" "$CONFIG_FILE")
        RUN_IN_MODES=$(jq -r ".configurations[$i].run_in_modes[]?" "$CONFIG_FILE" 2>/dev/null | tr '\n' ' ')
        
        if [ -z "$RUN_IN_MODES" ]; then
            RUN_IN_MODES="(all modes - default)"
        fi
        
        echo "  [$i] $CONFIG_NAME"
        echo "      Runs in: $RUN_IN_MODES"
    done
    exit 0
fi

# Function to run a single mode
run_single_mode() {
    local mode="$1"
    
    case $mode in
        bash)
            source "${SCRIPT_DIR}/stats_functions.sh"
            main_bash
            ;;
        counts)
            source "${SCRIPT_DIR}/counts_functions.sh"
            main_counts
            ;;
        *)
            echo "Error: Unknown mode '$mode'"
            return 1
            ;;
    esac
}

# Function to run a specific mode with specific configurations
run_mode_with_configs() {
    local mode="$1"
    local config_indices="$2"  # Space-separated list of indices
    
    echo "=================================================="
    echo "Running $mode mode with configurations: $config_indices"
    echo "=================================================="
    
    # Split config indices into array
    read -ra indices <<< "$config_indices"
    
    for idx in "${indices[@]}"; do
        # Extract name locally for display, though load_config_from_json will set the global
        local display_name=$(jq -r ".configurations[$idx].name" "$CONFIG_FILE")
        echo ""
        echo "++++++++++++++++++++++++++++++++++++++++++++++++++"
        echo "Configuration $idx: $display_name"
        echo "++++++++++++++++++++++++++++++++++++++++++++++++++"
        
        # Load configuration from JSON
        # This sets global variables including CONFIG_NAME and CURRENT_CONFIG_INDEX
        load_config_from_json "$CONFIG_FILE" "$idx"
        
        # Create executables for this configuration
        echo "Creating executables for $mode mode..."
        make clean
        
        # Run selected mode
        case $mode in
            bash | counts)
                run_single_mode "$mode"
                ;;
            all)
                echo -e "\n=== STATS mode ==="
                source "${SCRIPT_DIR}/stats_functions.sh"
                main_bash
                
                echo -e "\n=== COUNTS mode ==="
                source "${SCRIPT_DIR}/counts_functions.sh"
                main_counts
                ;;
        esac
        
        echo "Completed configuration $idx: $display_name"
        # Clear the result folders map so next config starts fresh
        deinitialize_folders
    done
}


# Function to run a specific mode with specific configurations
run_mode_with_filters() {
    local mode="$1"
    local config_indices="$2"  # Space-separated list of indices
    
    echo "=================================================="
    echo "Running $mode mode with configurations: $config_indices"
    echo "=================================================="
    
    # Split config indices into array
    read -ra indices <<< "$config_indices"
    
    for idx in "${indices[@]}"; do
        local display_name=$(jq -r ".configurations[$idx].name" "$CONFIG_FILE")
        echo ""
        echo "++++++++++++++++++++++++++++++++++++++++++++++++++"
        echo "Configuration $idx: $display_name"
        echo "++++++++++++++++++++++++++++++++++++++++++++++++++"
        
        # Load configuration from JSON
        load_config_from_json "$CONFIG_FILE" "$idx"
        
        # Create executables for this configuration
        echo "Creating executables for $mode mode..."
        make clean

        # Run selected mode
        case $mode in
            bash | counts)
                run_single_mode "$mode"
                ;;
            all)
                conf_modes=$(jq -r ".configurations[$idx].run_in_modes[]?" "$CONFIG_FILE" 2>/dev/null | tr '\n' ' ')
                for m in $conf_modes; do
                    echo -e "\n=== $m mode ==="
                    run_single_mode "$m"
                done
                ;;
        esac
        
        echo "Completed configuration $idx: $display_name"
        deinitialize_folders
    done
}

# Check jq is installed
check_jq

# Get all configurations
CONFIG_COUNT=$(jq '.configurations | length' "$CONFIG_FILE")

# Determine which configurations to run
if [ "$CONFIG_INDEX" = "filtered" ]; then
    echo "Selecting configurations for $MODE mode..."
    
    if [ "$MODE" = "all" ]; then
        # For "all" mode, run all configurations
        CONFIG_INDICES=$(seq 0 $((CONFIG_COUNT - 1)) | tr '\n' ' ')
        echo "Running ALL configurations for 'all' mode"
    else
        # Get configurations filtered by mode
        CONFIG_INDICES=$(get_configs_for_mode "$CONFIG_FILE" "$MODE")
        if [ -z "$CONFIG_INDICES" ]; then
            echo "Warning: No configurations found for $MODE mode. Running all configurations."
            CONFIG_INDICES=$(seq 0 $((CONFIG_COUNT - 1)) | tr '\n' ' ')
        else
            echo "Found configurations for $MODE mode: $CONFIG_INDICES"
        fi
    fi
    
elif [ "$CONFIG_INDEX" = "all" ]; then
    # Run all configurations regardless of mode
    CONFIG_INDICES=$(seq 0 $((CONFIG_COUNT - 1)) | tr '\n' ' ')
    echo "Running ALL configurations"
    
elif [[ "$CONFIG_INDEX" =~ ^[0-9]+$ ]]; then
    # Run single configuration
    if [ "$CONFIG_INDEX" -ge "$CONFIG_COUNT" ]; then
        echo "Error: Configuration index $CONFIG_INDEX not found. Available: 0-$((CONFIG_COUNT-1))"
        exit 1
    fi
    CONFIG_INDICES="$CONFIG_INDEX"
    
else
    echo "Error: Invalid configuration index '$CONFIG_INDEX'. Must be a number, 'filtered', or 'all'."
    exit 1
fi

# Display which configurations will be run
echo ""
echo "Configuration summary:"
echo "  Mode: $MODE"
echo "  Config file: $CONFIG_FILE"
echo "  Configurations to run: $CONFIG_INDICES"
echo ""

# Convert indices to array and display details
read -ra indices <<< "$CONFIG_INDICES"
for idx in "${indices[@]}"; do
    CONFIG_NAME=$(jq -r ".configurations[$idx].name" "$CONFIG_FILE")
    RUN_IN_MODES=$(jq -r ".configurations[$idx].run_in_modes[]?" "$CONFIG_FILE" 2>/dev/null | tr '\n' ' ')
    
    if [ -z "$RUN_IN_MODES" ]; then
        RUN_IN_MODES="all modes"
    fi
    
    echo "  [$idx] $CONFIG_NAME (runs in: $RUN_IN_MODES)"
done
echo ""

# Ask for confirmation if more than one configuration
if [ ${#indices[@]} -gt 1 ]; then
    read -p "Run ${#indices[@]} configurations? (y/N): " -n 1 -r
    echo
    if [[ ! $REPLY =~ ^[Yy]$ ]]; then
        echo "Aborted."
        exit 0
    fi
fi

# Run the mode with selected configurations
if [ "$CONFIG_INDEX" = "filtered" ]; then
    run_mode_with_filters "$MODE" "$CONFIG_INDICES"
else
    run_mode_with_configs "$MODE" "$CONFIG_INDICES"
fi

echo -e "\nAll operations completed!"