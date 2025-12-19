#!/bin/bash
# Run reducer tests on host (no hardware needed)
set -e

echo "Running tests on host..."
cargo test --lib --no-default-features --target aarch64-apple-darwin "$@"
