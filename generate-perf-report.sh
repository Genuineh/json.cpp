#!/bin/bash
#
# Performance Report Generator for json.cpp
# Generates comprehensive performance reports in multiple formats
#

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build"
PERF_BIN="${BUILD_DIR}/json_perf"
REPORT_DIR="${SCRIPT_DIR}/perf-reports"

# Default configuration
RUNS=10
WARMUP=2
FORMATS=("text" "csv" "json" "markdown")
TIMESTAMP=$(date +%Y%m%d-%H%M%S)

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

print_info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

print_warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

print_usage() {
    cat << EOF
Performance Report Generator for json.cpp

Usage: $0 [OPTIONS]

Options:
    -r, --runs N        Number of measurement runs (default: 10)
    -w, --warmup N      Number of warmup runs (default: 2)
    -o, --output DIR    Output directory for reports (default: perf-reports)
    -f, --format FMT    Report format: text, csv, json, markdown, all (default: all)
    --filter PATTERN    Only run benchmarks matching pattern
    --quick             Quick run with fewer iterations (runs=3, warmup=1)
    --thorough          Thorough run with many iterations (runs=50, warmup=10)
    --baseline          Save results as baseline for future comparisons
    --compare FILE      Compare against baseline file (JSON format)
    -h, --help          Show this help message

Examples:
    $0                                  # Generate all reports with default settings
    $0 --quick --format markdown        # Quick markdown report
    $0 --thorough --baseline            # Thorough run saved as baseline
    $0 --runs 20 --filter parse         # Only parsing benchmarks
    $0 --compare baseline.json          # Compare with baseline

Output:
    Reports are saved to the output directory with timestamp:
    - perf-report-{timestamp}.txt
    - perf-report-{timestamp}.csv
    - perf-report-{timestamp}.json
    - perf-report-{timestamp}.md

EOF
}

# Parse command line arguments
FILTER=""
OUTPUT_DIR=""
FORMAT_ARG=""
SAVE_BASELINE=false
COMPARE_FILE=""

while [[ $# -gt 0 ]]; do
    case $1 in
        -r|--runs)
            RUNS="$2"
            shift 2
            ;;
        -w|--warmup)
            WARMUP="$2"
            shift 2
            ;;
        -o|--output)
            OUTPUT_DIR="$2"
            shift 2
            ;;
        -f|--format)
            FORMAT_ARG="$2"
            shift 2
            ;;
        --filter)
            FILTER="$2"
            shift 2
            ;;
        --quick)
            RUNS=3
            WARMUP=1
            shift
            ;;
        --thorough)
            RUNS=50
            WARMUP=10
            shift
            ;;
        --baseline)
            SAVE_BASELINE=true
            shift
            ;;
        --compare)
            COMPARE_FILE="$2"
            shift 2
            ;;
        -h|--help)
            print_usage
            exit 0
            ;;
        *)
            print_error "Unknown option: $1"
            print_usage
            exit 1
            ;;
    esac
done

# Set output directory
if [ -z "$OUTPUT_DIR" ]; then
    OUTPUT_DIR="$REPORT_DIR"
fi

# Create output directory
mkdir -p "$OUTPUT_DIR"

# Check if benchmark binary exists
if [ ! -f "$PERF_BIN" ]; then
    print_error "Benchmark binary not found: $PERF_BIN"
    print_info "Building benchmarks..."
    cd "$SCRIPT_DIR"
    ./build.sh perf --no-test
    if [ ! -f "$PERF_BIN" ]; then
        print_error "Failed to build benchmark binary"
        exit 1
    fi
fi

print_info "Performance Report Generation"
print_info "=============================="
print_info "Runs: $RUNS"
print_info "Warmup: $WARMUP"
print_info "Output: $OUTPUT_DIR"
if [ -n "$FILTER" ]; then
    print_info "Filter: $FILTER"
fi
echo ""

# Determine which formats to generate
if [ "$FORMAT_ARG" == "all" ] || [ -z "$FORMAT_ARG" ]; then
    SELECTED_FORMATS=("${FORMATS[@]}")
else
    SELECTED_FORMATS=("$FORMAT_ARG")
fi

# Build common arguments
COMMON_ARGS="--runs $RUNS --warmup $WARMUP"
if [ -n "$FILTER" ]; then
    COMMON_ARGS="$COMMON_ARGS --filter $FILTER"
fi

# Generate reports in each format
for format in "${SELECTED_FORMATS[@]}"; do
    case $format in
        text)
            EXT="txt"
            ;;
        csv)
            EXT="csv"
            ;;
        json)
            EXT="json"
            ;;
        markdown|md)
            EXT="md"
            format="markdown"
            ;;
        *)
            print_warn "Unknown format: $format, skipping"
            continue
            ;;
    esac
    
    OUTPUT_FILE="${OUTPUT_DIR}/perf-report-${TIMESTAMP}.${EXT}"
    print_info "Generating $format report..."
    
    if [ "$format" == "text" ]; then
        # Text format doesn't use --report flag
        $PERF_BIN $COMMON_ARGS > "$OUTPUT_FILE"
    else
        $PERF_BIN $COMMON_ARGS --report "$format" > "$OUTPUT_FILE"
    fi
    
    print_info "Saved: $OUTPUT_FILE"
done

# Save as baseline if requested
if [ "$SAVE_BASELINE" = true ]; then
    BASELINE_FILE="${OUTPUT_DIR}/baseline.json"
    print_info "Saving baseline to: $BASELINE_FILE"
    $PERF_BIN $COMMON_ARGS --report json > "$BASELINE_FILE"
    print_info "Baseline saved successfully"
fi

# Compare with baseline if requested
if [ -n "$COMPARE_FILE" ]; then
    if [ ! -f "$COMPARE_FILE" ]; then
        print_error "Baseline file not found: $COMPARE_FILE"
        exit 1
    fi
    
    print_info "Comparing with baseline: $COMPARE_FILE"
    CURRENT_FILE="${OUTPUT_DIR}/current-${TIMESTAMP}.json"
    $PERF_BIN $COMMON_ARGS --report json > "$CURRENT_FILE"
    
    # Simple comparison (could be enhanced with a Python/Node script)
    print_info "Current results saved to: $CURRENT_FILE"
    print_warn "Automatic comparison not yet implemented"
    print_info "Please manually compare $COMPARE_FILE with $CURRENT_FILE"
fi

echo ""
print_info "Report generation complete!"
print_info "Reports saved to: $OUTPUT_DIR"

# Print summary
echo ""
echo -e "${BLUE}Summary of Generated Files:${NC}"
ls -lh "$OUTPUT_DIR"/perf-report-${TIMESTAMP}.* 2>/dev/null | awk '{print "  " $9 " (" $5 ")"}'

exit 0
