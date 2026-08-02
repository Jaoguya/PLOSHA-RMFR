#!/bin/bash
set -e
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SRC_DIR="$(cd "$SCRIPT_DIR/../src" && pwd)"
DATASET="$(cd "$SCRIPT_DIR/../../../dataset" && pwd)/plosha_dataset.csv"
BIN="$SRC_DIR/ft_serverless_sim"

if [ ! -f "$BIN" ]; then
    g++ -O2 -std=c++17 -o "$BIN" "$SRC_DIR"/main.cpp "$SRC_DIR"/ft_network.cpp "$SRC_DIR"/ft_algorithms.cpp "$SRC_DIR"/ft_experiments.cpp
fi

OUTPUT="$SCRIPT_DIR/results.csv"
echo "variable_value,primary_metric,secondary_metric_1,secondary_metric_2" > "$OUTPUT"
for s in 1 2 3 4 5 6 7 8 9 10; do
    "$BIN" --experiment 6 --variable $s --cloudlets 10 --sensors 1000 --dataset "$DATASET" >> "$OUTPUT"
done
echo "exp6 done: $OUTPUT"
