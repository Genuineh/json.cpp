# json.cpp Performance Benchmarks

This directory contains the performance benchmarking infrastructure for json.cpp.

## Files

- **json_perf.cpp** - Main benchmark program with comprehensive test suite
- **corpus/** - Sample JSON files used for testing
  - `medium_orders.json` - ~196KB realistic e-commerce data
  - `large_orders.json` - ~1.6MB large-scale test data

## Quick Start

### Running Benchmarks

```bash
# From project root
./build.sh perf

# Or directly
./build/json_perf --runs 10 --warmup 2
```

### Available Benchmarks

The suite includes 24 comprehensive benchmarks across multiple categories:

#### Parsing (9 benchmarks)
- `parse.small_literal` - Small JSON literal (~767 bytes)
- `parse.medium_orders` - Medium document (~196KB)
- `parse.large_orders` - Large document (~1.6MB)
- `parse.corpus_valid` - JSONTestSuite valid cases
- `parse.corpus_invalid` - Invalid JSON rejection
- `parse.deeply_nested` - 15-level nested objects
- `parse.number_array` - 100 floating-point numbers
- `parse.string_array` - 50 string values
- `parse.invalid_deep_array` - Depth limit testing

#### Serialization (4 benchmarks)
- `stringify.small_compact` - Compact output
- `stringify.small_pretty` - Pretty-printed output
- `stringify.large_compact` - Large document compact
- `stringify.escape_heavy` - Heavy escape sequences

#### Construction (3 benchmarks)
- `construct.empty_object` - Simple object creation
- `construct.nested_object` - Deep nesting construction
- `construct.array_integers` - Integer array building

#### Access (2 benchmarks)
- `access.deep_nested` - Deep property access
- `access.array_iteration` - Array element iteration

#### JSONPath (4 benchmarks)
- `jsonpath.query_authors` - Path query operations
- `jsonpath.filter_prices` - Filter expressions
- `jsonpath.update_prices` - Bulk updates
- `jsonpath.delete_isbn` - Deletion operations

#### Other (2 benchmarks)
- `roundtrip.medium_orders` - Parse + serialize cycle
- `copy.medium_object` - Deep copy operations

## Options

```bash
json_perf [OPTIONS]

Options:
  --warmup N       Number of warmup runs (default: 1)
  --runs N         Number of measurement runs (default: 5)
  --scale X        Scale iteration counts by X
  --filter STR     Only run benchmarks containing STR
  --list           List all benchmark names
  --report FORMAT  Generate report (text, csv, json, markdown)
```

## Examples

### List All Benchmarks
```bash
./build/json_perf --list
```

### Run Specific Category
```bash
# Only parsing benchmarks
./build/json_perf --filter parse

# Only JSONPath benchmarks
./build/json_perf --filter jsonpath
```

### Generate Reports

#### CSV Report
```bash
./build/json_perf --runs 10 --report csv > results.csv
```

#### JSON Report
```bash
./build/json_perf --runs 10 --report json > results.json
```

#### Markdown Report
```bash
./build/json_perf --runs 10 --report markdown > RESULTS.md
```

### Performance Testing

#### Quick Test
```bash
./build/json_perf --runs 3 --warmup 1 --scale 0.5
```

#### Production Profile
```bash
./build/json_perf --runs 100 --warmup 10
```

## Automated Report Generation

Use the convenience script for comprehensive reporting:

```bash
# Quick report (all formats)
../generate-perf-report.sh --quick

# Detailed report (markdown only)
../generate-perf-report.sh --runs 20 --format markdown

# Baseline for regression testing
../generate-perf-report.sh --thorough --baseline

# Compare with baseline
../generate-perf-report.sh --compare perf-reports/baseline.json
```

## Understanding Metrics

Each benchmark reports:

- **Mean (ns/op)** - Average time per operation
- **Median (ns/op)** - Middle value, less affected by outliers
- **Min (ns/op)** - Best case performance
- **Max (ns/op)** - Worst case performance
- **StdDev (ns/op)** - Standard deviation (consistency measure)
- **P95 (ns/op)** - 95th percentile (tail latency)
- **P99 (ns/op)** - 99th percentile (worst 1% cases)
- **Throughput (MB/s)** - Processing rate (where applicable)

## Typical Results

On AMD Threadripper PRO 7995WX with GCC 13.2:

| Operation | Median Time | Throughput |
|-----------|-------------|------------|
| Parse small | ~5.4 μs | 143 MB/s |
| Parse medium | ~1.9 ms | 103 MB/s |
| Parse large | ~16 ms | 100 MB/s |
| Stringify compact | ~681 μs | 164 MB/s |
| JSONPath query | ~340 ns | - |
| Deep object access | ~365 ns | - |

## Adding New Benchmarks

To add a new benchmark to `json_perf.cpp`:

```cpp
cases.push_back({ 
    "category.benchmark_name",  // Descriptive name
    1000,                        // Inner iterations
    bytes_per_iteration,         // Size for throughput (0 if N/A)
    prepare_function,            // Setup (or nullptr)
    [&]() {
        // Benchmark body
        DoNotOptimize(result);   // Prevent optimization
        g_sink += result;        // Prevent dead code elimination
    }
});
```

## Best Practices

1. **Warmup** - Always use warmup runs to stabilize CPU frequency
2. **Multiple runs** - Use at least 5-10 runs for statistical validity
3. **Filter tests** - Focus on relevant benchmarks during development
4. **Report formats** - Use JSON/CSV for automated analysis
5. **Baseline tracking** - Save baseline results for regression detection

## Integration with CI/CD

Example GitHub Actions workflow:

```yaml
- name: Performance benchmarks
  run: |
    ./build.sh perf -- --runs 10 --report json > perf.json
    
- name: Upload results
  uses: actions/upload-artifact@v3
  with:
    name: performance-results
    path: perf.json
```

## Profiling

For detailed analysis:

```bash
# CPU profiling
perf record -g ./build/json_perf --runs 1 --filter parse.large
perf report

# Cache analysis
perf stat -e cache-references,cache-misses \
  ./build/json_perf --runs 10

# Memory profiling
valgrind --tool=massif ./build/json_perf --runs 1 --scale 0.01
```

## See Also

- [PERFORMANCE.md](../PERFORMANCE.md) - Comprehensive performance guide
- [BENCHMARK_SAMPLE.md](../BENCHMARK_SAMPLE.md) - Sample benchmark results
- [README.md](../README.md) - Main project documentation
