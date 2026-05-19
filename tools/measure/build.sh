#!/usr/bin/env bash
# PARA-3 :: build the lab measurement driver
#
# Usage (auf dem VPS, im Repo-Root):
#   bash tools/measure/build.sh
#   /tmp/measure                  # alle Messungen
#   /tmp/measure M1               # nur Section 1
#   /tmp/measure M1.1             # nur eine Messung

set -e
cd "$(dirname "$0")/../.."
mkdir -p docs/measurements
g++ -O2 -std=c++17 -Wall -Wextra -msse2 -I. \
    tools/measure/measure_main.cpp \
    -o /tmp/measure
echo "Built /tmp/measure. Run it from the repo root to write into docs/measurements/."
