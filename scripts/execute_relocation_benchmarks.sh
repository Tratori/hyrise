#!/bin/bash

# Set the environment variables
RELOCATE_GRANULARITY_LIST=("chunk" "table")
SCHEDULING_OPTIMIZATIONS_LIST=("on" "off")
SCHEDULE_DISTANT_NODE_LIST=("on" "off")
QUERY_EXECUTION_LIST=("" "-q 5,9,21")
SCALEFACTOR=("0.1")


# First we create our dataset
../build-cmake-release/hyriseBenchmarkTPCH --scale "10" --scheduler --mode=Shuffled --clients=60 --time=1200 --output "relocation_benchmarks_new_data_generation.json" | \
tee "relocation_benchmarks_new_data_generation.txt"

# load from binary
../build-cmake-release/hyriseBenchmarkTPCH --scale "10" --scheduler --mode=Shuffled --clients=60 --time=1200 --output "relocation_benchmarks_binary_loading.json" | \
tee "relocation_benchmarks_binary_loading.txt"


export RELOCATE_GRANULARITY="chunk"
../build-cmake-release/hyriseBenchmarkTPCH --scale "10" --relocate_numa --scheduler --mode=Shuffled --clients=60 --time=1200 --output "relocation_benchmarks_chunk_rr.json" | \
tee "relocation_benchmarks_chunk_rr.txt"

export RELOCATE_GRANULARITY="table"
../build-cmake-release/hyriseBenchmarkTPCH --scale "10" --relocate_numa --scheduler --mode=Shuffled --clients=60 --time=1200 --output "relocation_benchmarks_table_rr.json" | \
tee "relocation_benchmarks_table_rr.txt"
