#!/bin/bash
# Counts-mode runners (stats macros / counts_*.jsonl).
source "${SCRIPT_DIR}/common.sh"

create_executables() {
    make clean
    local stats_string=$(build_stats_flags_string)
    echo "Building with statistics flags: $stats_string"
    make all STATS="$stats_string" PINNING="$PINNING_FLAG" SNUMA_SUPPORT="$SNUMA_SUPPORT"
}

clean_statistics_files() {
    make clean_statistics
}

is_stat_enabled() {
    local flag=$1
    for enabled_flag in "${STATS_FLAGS[@]}"; do
        if [ "$enabled_flag" = "$flag" ]; then return 0; fi
    done
    return 1
}

run_test_suite_counts() {
    local program=$1
    local test_num=$2
    local test_name=$3
    local op_percentage=$4
    local del_percentage=$5
    local op_count=$6
    local output_folder=$7

    # Specific file for this test
    local output_file="${output_folder}/counts_${test_name}.jsonl"

    echo "Running (COUNTS) $program - $test_name"
    
    local compute_stats=$(is_stat_enabled "COMPUTE_STATS" && echo "yes" || echo "no")
    local compute_height=$(is_stat_enabled "COMPUTE_HEIGHT" && echo "yes" || echo "no")
    local compute_skiplist_levels=$(is_stat_enabled "COMPUTE_SKIPLIST_LEVELS" && echo "yes" || echo "no")
    local compute_avg=$(is_stat_enabled "COMPUTE_AVG" && echo "yes" || echo "no")
    local compute_atomics=$(is_stat_enabled "COMPUTE_ATOMIC" && echo "yes" || echo "no")
    local compute_combiner=$(is_stat_enabled "COMPUTE_COMBINER" && echo "yes" || echo "no")
    local compute_hw_events=$(is_stat_enabled "COMPUTE_HW_EVENTS" && echo "yes" || echo "no")
    
    for threads in "${THREADS[@]}"; do
        for ((i=1; i<=NUM_RUNS; i++)); do
            echo -e "Threads [$threads] Run [$i]..."
            local output=$(./"$program".out \
                -t "$threads" -T "$MAX_THREADS" -c "$CORES" \
                -p "$op_percentage" -e "$del_percentage" \
                -i "$test_num" -o "$op_count" -l "$LESS_OP" \
                -b "$OP_BETWEEN" -m "$TIME_MAIN" -s "$TIME_SECONDARY")
            
            # Parse Results (OPS & TIME)
            local result_ops=$(echo "$output" | awk '/^\[RESULTS_OPS\]/ {print $2}')
            local result_time=$(echo "$output" | awk '/^\[RESULTS_TIME\]/ {print $2}')
            [ -z "$result_ops" ] && result_ops=0
            [ -z "$result_time" ] && result_time=0

            # Parse Stats
            local alloc=0; local alloc_size=0; local del_ret=0; local ins_ret=0; local ins=0; local dels=0; local search=0; local rebalance=0
            local level=0
            local height_min=0; local height_max=0
            local pointers_traversed=0; local pointers_ins_traversed=0; local pointers_reb_traversed=0; local external_pointers_traversed=0
            local num_atomics=0
            local num_external_atomics=0
            local combiner_changes=0
            
            # Default values for all Hardware Events
            local task_clock=0; local context_switches=0; local cpu_migrations=0; local page_faults=0
            local major_faults=0; local minor_faults=0; local cycles=0; local instructions=0
            local branches=0; local branch_misses=0; local l1i_misses=0; local l1d_misses=0
            local l2_misses=0; local llc_loads=0; local llc_misses=0; local itlb_misses=0
            local dtlb_load_misses=0; local dtlb_store_misses=0; local machine_clears_memory_ordering=0
            local cycle_activity_stalls_total=0; local cycle_activity_stalls_mem_any=0

            if [ "$compute_stats" = "yes" ]; then
                alloc=$(echo "$output" | awk '/Total allocations:/ {print $3}' | grep -o '[0-9]*' || echo 0)
                alloc_size=$(echo "$output" | awk '/Total allocated size:/ {print $4}' | grep -o '[0-9]*' || echo 0)
                del_ret=$(echo "$output" | awk '/Total delete retries:/ {print $4}' | grep -o '[0-9]*' || echo 0)
                ins_ret=$(echo "$output" | awk '/Total insert retries:/ {print $4}' | grep -o '[0-9]*' || echo 0)
                ins=$(echo "$output" | awk '/Total Inserts:/ {print $3}' | grep -o '[0-9]*' || echo 0)
                dels=$(echo "$output" | awk '/Total Deletes:/ {print $3}' | grep -o '[0-9]*' || echo 0)
                search=$(echo "$output" | awk '/Total Locates:/ {print $3}' | grep -o '[0-9]*' || echo 0)
                rebalance=$(echo "$output" | awk '/Total Rebalances:/ {print $3}' | grep -o '[0-9]*' || echo 0)
            fi

            if [ "$compute_skiplist_levels" = "yes" ]; then
                level=$(echo "$output" | awk '/Skiplist levels:/ {print $3}' | grep -o '[0-9]*' || echo 0)
            fi

            if [ "$compute_height" = "yes" ]; then
                height_min=$(echo "$output" | awk '/Tree height Min:/ {print $4}' | grep -o '[0-9]*' || echo 0)
                height_max=$(echo "$output" | awk '/Tree height Max:/ {print $4}' | grep -o '[0-9]*' || echo 0)
            fi

            if [ "$compute_avg" = "yes" ]; then
                pointers_traversed=$(echo "$output" | awk '/Pointers Delete traversed:/ {print $4}' | grep -o '[0-9]*' || echo 0)
                pointers_ins_traversed=$(echo "$output" | awk '/Pointers Insert traversed:/ {print $4}' | grep -o '[0-9]*' || echo 0)
                pointers_reb_traversed=$(echo "$output" | awk '/Pointers Rebalance traversed:/ {print $4}' | grep -o '[0-9]*' || echo 0)
                external_pointers_traversed=$(echo "$output" | awk '/External Pointers traversed:/ {print $4}' | grep -o '[0-9]*' || echo 0)
            fi

            if [ "$compute_atomics" = "yes" ]; then
                num_atomics=$(echo "$output" | awk '/Number of Atomic Operations:/ {print $5}' | grep -o '[0-9]*' || echo 0)
                num_external_atomics=$(echo "$output" | awk '/Number of External Atomic Operations:/ {print $6}' | grep -o '[0-9]*' || echo 0)
            fi
            
            if [ "$compute_combiner" = "yes" ]; then
                combiner_changes=$(echo "$output" | awk '/Number of Combiner Changes:/ {print $5}' | grep -o '[0-9]*' || echo 0)
            fi
            
            if [ "$compute_hw_events" = "yes" ]; then
                task_clock=$(echo "$output" | awk '/Task Clock \(ns\):/ {print $4}' | grep -o '[0-9]*' || echo 0)
                context_switches=$(echo "$output" | awk '/Context Switches:/ {print $3}' | grep -o '[0-9]*' || echo 0)
                cpu_migrations=$(echo "$output" | awk '/CPU Migrations:/ {print $3}' | grep -o '[0-9]*' || echo 0)
                page_faults=$(echo "$output" | awk '/Page Faults:/ {print $3}' | grep -o '[0-9]*' || echo 0)
                major_faults=$(echo "$output" | awk '/Major Faults:/ {print $3}' | grep -o '[0-9]*' || echo 0)
                minor_faults=$(echo "$output" | awk '/Minor Faults:/ {print $3}' | grep -o '[0-9]*' || echo 0)
                cycles=$(echo "$output" | awk '/Cycles:/ {print $2}' | grep -o '[0-9]*' || echo 0)
                instructions=$(echo "$output" | awk '/Instructions:/ {print $2}' | grep -o '[0-9]*' || echo 0)
                branches=$(echo "$output" | awk '/Branches:/ {print $2}' | grep -o '[0-9]*' || echo 0)
                branch_misses=$(echo "$output" | awk '/Branch Misses:/ {print $3}' | grep -o '[0-9]*' || echo 0)
                l1i_misses=$(echo "$output" | awk '/L1I Cache Misses:/ {print $4}' | grep -o '[0-9]*' || echo 0)
                l1d_misses=$(echo "$output" | awk '/L1D Cache Misses:/ {print $4}' | grep -o '[0-9]*' || echo 0)
                l2_misses=$(echo "$output" | awk '/L2 Cache Misses \(Raw\):/ {print $5}' | grep -o '[0-9]*' || echo 0)
                llc_loads=$(echo "$output" | awk '/LLC Loads:/ {print $3}' | grep -o '[0-9]*' || echo 0)
                llc_misses=$(echo "$output" | awk '/LLC Load Misses:/ {print $4}' | grep -o '[0-9]*' || echo 0)
                itlb_misses=$(echo "$output" | awk '/iTLB Load Misses:/ {print $4}' | grep -o '[0-9]*' || echo 0)
                dtlb_load_misses=$(echo "$output" | awk '/dTLB Load Misses:/ {print $4}' | grep -o '[0-9]*' || echo 0)
                dtlb_store_misses=$(echo "$output" | awk '/dTLB Store Misses:/ {print $4}' | grep -o '[0-9]*' || echo 0)
                machine_clears_memory_ordering=$(echo "$output" | awk '/Machine Clears \(Memory Ordering\):/ {print $5}' | grep -o '[0-9]*' || echo 0)
                cycle_activity_stalls_total=$(echo "$output" | awk '/Cycle Activity Stalls Total:/ {print $5}' | grep -o '[0-9]*' || echo 0)
                cycle_activity_stalls_mem_any=$(echo "$output" | awk '/Cycle Activity Stalls Mem Any:/ {print $6}' | grep -o '[0-9]*' || echo 0)
            fi
            
            local metrics_json=$(jq -n -c \
                --argjson rs_ops "$result_ops" \
                --argjson rs_time "$result_time" \
                --argjson al "$alloc" \
                --argjson als "$alloc_size" \
                --argjson dr "$del_ret" \
                --argjson ir "$ins_ret" \
                --argjson ns "$search" \
                --argjson lv "$level" \
                --argjson ins "$ins" \
                --argjson reb "$rebalance" \
                --argjson hin "$height_min" \
                --argjson hax "$height_max" \
                --argjson pt "$pointers_traversed" \
                --argjson dels "$dels" \
                --argjson pit "$pointers_ins_traversed" \
                --argjson prt "$pointers_reb_traversed" \
                --argjson ept "$external_pointers_traversed" \
                --argjson nat "$num_atomics" \
                --argjson neat "$num_external_atomics" \
                --argjson cch "$combiner_changes" \
                --argjson tcl "$task_clock" \
                --argjson csw "$context_switches" \
                --argjson cpm "$cpu_migrations" \
                --argjson pf "$page_faults" \
                --argjson mjf "$major_faults" \
                --argjson mnf "$minor_faults" \
                --argjson cyc "$cycles" \
                --argjson inst "$instructions" \
                --argjson br "$branches" \
                --argjson brm "$branch_misses" \
                --argjson l1im "$l1i_misses" \
                --argjson l1dm "$l1d_misses" \
                --argjson l2m "$l2_misses" \
                --argjson llcl "$llc_loads" \
                --argjson llcm "$llc_misses" \
                --argjson itm "$itlb_misses" \
                --argjson dtlm "$dtlb_load_misses" \
                --argjson dtsm "$dtlb_store_misses" \
                --argjson mcmo "$machine_clears_memory_ordering" \
                --argjson cast "$cycle_activity_stalls_total" \
                --argjson casma "$cycle_activity_stalls_mem_any" \
                '{
                    result_ops: $rs_ops,
                    result_time: $rs_time,
                    total_allocations: $al,
                    total_allocated_size: $als,
                    delete_retries: $dr,
                    insert_retries: $ir,
                    total_searches: $ns,
                    skiplist_levels: $lv,
                    total_inserts: $ins,
                    total_rebalance: $reb,
                    tree_height_min: $hin,
                    tree_height_max: $hax,
                    pointers_traversed: $pt,
                    total_deletes: $dels,
                    pointers_ins_traversed: $pit,
                    pointers_reb_traversed: $prt,
                    external_pointers_traversed: $ept,
                    num_atomics: $nat,
                    num_external_atomics: $neat,
                    combiner_changes: $cch,
                    task_clock: $tcl,
                    context_switches: $csw,
                    cpu_migrations: $cpm,
                    page_faults: $pf,
                    major_faults: $mjf,
                    minor_faults: $mnf,
                    cycles: $cyc,
                    instructions: $inst,
                    branches: $br,
                    branch_misses: $brm,
                    L1_icache_load_misses: $l1im,
                    L1_dcache_load_misses: $l1dm,
                    l2_lines_in_all: $l2m,
                    LLC_loads: $llcl,
                    LLC_load_misses: $llcm,
                    iTLB_load_misses: $itm,
                    dTLB_load_misses: $dtlm,
                    dTLB_store_misses: $dtsm,
                    machine_clears_memory_ordering: $mcmo,
                    cycle_activity_stalls_total: $cast,
                    cycle_activity_stalls_mem_any: $casma
                }')
                
            append_to_jsonl "$output_file" "$test_name" "$program" "$threads" "$i" "$op_count" "$op_percentage" "$del_percentage" "$metrics_json"
        done
    done
}

main_counts() {
    echo "Using statistics-enabled executables..."
    create_executables
    common_main "run_test_suite_counts"
    clean_statistics_files
}