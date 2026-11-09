#!/bin/bash
# Throughput (stats_*.jsonl) experiment runners.
source "${SCRIPT_DIR}/common.sh"

create_executables() {
    make clean
    make all STATS='' PINNING="$PINNING_FLAG" SNUMA_SUPPORT="$SNUMA_SUPPORT"
}

run_test_suite_bash() {
    local program=$1
    local test_num=$2
    local test_name=$3
    local op_percentage=$4
    local del_percentage=$5
    local op_count=$6
    local output_folder=$7
    
    local output_file="${output_folder}/stats_${test_name}.jsonl"
    
    echo "Running (BASH) $program - $test_name"

    for threads in "${THREADS[@]}"; do
        for ((i=1; i<=NUM_RUNS; i++)); do
            echo -e "Threads [$threads] Run [$i]..."
            
            local output=$(./"$program".out \
                -t "$threads" -T "$MAX_THREADS" -c "$CORES" \
                -p "$op_percentage" -e "$del_percentage" \
                -i "$test_num" -o "$op_count" -l "$LESS_OP" \
                -b "$OP_BETWEEN" -m "$TIME_MAIN" -s "$TIME_SECONDARY")
            
            # Parse OPS & TIME
            local result_ops=$(echo "$output" | awk '/^\[RESULTS_OPS\]/ {print $2}')
            local result_time=$(echo "$output" | awk '/^\[RESULTS_TIME\]/ {print $2}')
            
            if [ -n "$result_ops" ] && [ -n "$result_time" ]; then
                local metrics_json="{\"result_ops\": $result_ops, \"result_time\": $result_time}"
                append_to_jsonl "$output_file" "$test_name" "$program" "$threads" "$i" "$op_count" "$op_percentage" "$del_percentage" "$metrics_json"
            else
                echo "[ERROR] No result for $program T=$threads R=$i"
            fi
        done
    done
}

main_bash() {
    echo "Using regular executables (without statistics)..."
    create_executables
    common_main "run_test_suite_bash"
}