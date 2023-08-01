#!/bin/bash

# Set the environment variables
RELOCATE_GRANULARITY_LIST=("chunk" "table")
SCHEDULING_OPTIMIZATIONS_LIST=("on" "off")
SCHEDULE_DISTANT_NODE_LIST=("on" "off")
QUERY_EXECUTION=("" "-q 5,9,21")
SCALEFACTOR=("0.1")

# Function to get the query execution type string
get_query_exec_type() {
    if [[ -z "$1" ]]; then
        echo "all"
    elif [[ "$1" == "-q 5,9,21" ]]; then
        echo "limited"
    else
        echo "unknown"
    fi
}

# Outermost loop to iterate through the scale factors
for SF in "${SCALEFACTOR[@]}"; do
    for RG in "${RELOCATE_GRANULARITY_LIST[@]}"; do
        for SO in "${SCHEDULING_OPTIMIZATIONS_LIST[@]}"; do
            for SDN in "${SCHEDULE_DISTANT_NODE_LIST[@]}"; do
                for QEXEC in "${QUERY_EXECUTION[@]}"; do
                    # Get the query execution type string
                    QUERY_EXEC_TYPE=$(get_query_exec_type "$QEXEC")
                    export RELOCATE_GRANULARITY="$RG"
                    export SCHEDULING_OPTIMIZATIONS="$SO"
                    export SCHEDULE_DISTANT_NODE="$SDN"
                    export SCALE="$SF"

                    (
                        ../build-cmake-release/hyriseBenchmarkTPCH --scale "$SCALE" --relocate_numa --scheduler --mode=Shuffled --clients=60 --time=1 $QUERY_EXECUTION --output "numa_merged_${RELOCATE_GRANULARITY}_SCHEDULING_OPTIMIZATIONS_${SCHEDULING_OPTIMIZATIONS}_SCHEDULE_DISTANT_NODE_${SCHEDULE_DISTANT_NODE}_queries_${QUERY_EXEC_TYPE}_${SCALE}.json" | \
                        tee "numa_merged_${RELOCATE_GRANULARITY}_SCHEDULING_OPTIMIZATIONS_${SCHEDULING_OPTIMIZATIONS}_SCHEDULE_DISTANT_NODE_${SCHEDULE_DISTANT_NODE}_queries_${QUERY_EXEC_TYPE}_${SCALE}.txt"
                    )
                done
            done
        done
    done
done