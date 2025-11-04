# Performance Guide

This document provides comprehensive information about json.cpp performance characteristics, benchmarking methodology, and optimization guidelines.

## Quick Start

Run performance benchmarks with:

```bash
./build.sh perf
```

Or build and run directly:

```bash
cmake --build build --target json_perf
./build/json_perf --runs 10 --warmup 2
```

## Benchmark Options

The `json_perf` tool provides several options for customized benchmarking:

| Option | Description | Default |
|--------|-------------|---------|
| `--warmup N` | Number of warmup iterations before measurement | 1 |
| `--runs N` | Number of measurement runs for statistics | 5 |
| `--scale X` | Scale iteration counts by factor X | 1.0 |
| `--filter STR` | Only run benchmarks containing STR in name | (none) |
| `--list` | List all available benchmark names | - |
| `--report FORMAT` | Generate report in specified format | text |

### Report Formats

The tool supports multiple output formats for automated processing and analysis:

#### Text Format (default)
Human-readable text output with detailed statistics:
```bash
./build/json_perf --runs 10
```

#### CSV Format
Machine-readable CSV for spreadsheet import:
```bash
./build/json_perf --runs 10 --report csv > results.csv
```

#### JSON Format
Structured JSON for programmatic analysis:
```bash
./build/json_perf --runs 10 --report json > results.json
```

#### Markdown Format
GitHub-friendly markdown tables for documentation:
```bash
./build/json_perf --runs 10 --report markdown > BENCHMARK_RESULTS.md
```

## Benchmark Categories

### Parsing Benchmarks

- **parse.small_literal** - Parse small JSON literal (~767 bytes)
- **parse.medium_orders** - Parse medium-sized JSON document (~196KB)
- **parse.large_orders** - Parse large JSON document (~1.6MB)
- **parse.corpus_valid** - Parse entire JSONTestSuite valid corpus
- **parse.corpus_invalid** - Rejection speed for invalid JSON
- **parse.deeply_nested** - Parse deeply nested objects (15 levels)
- **parse.number_array** - Parse array of 100 floating-point numbers
- **parse.string_array** - Parse array of 50 string values
- **parse.invalid_deep_array** - Early rejection of overly deep structures

### Serialization Benchmarks

- **stringify.small_compact** - Compact serialization of medium document
- **stringify.small_pretty** - Pretty-printed serialization with indentation
- **stringify.large_compact** - Compact serialization of large document
- **stringify.escape_heavy** - Serialization with many escape sequences

### Construction Benchmarks

- **construct.empty_object** - Create and populate a simple object
- **construct.nested_object** - Create deeply nested object structure
- **construct.array_integers** - Construct array with 10 integer elements

### Access Benchmarks

- **access.deep_nested** - Access deeply nested object properties
- **access.array_iteration** - Iterate through array elements

### JSONPath Benchmarks

- **jsonpath.query_authors** - Query all book authors using JSONPath
- **jsonpath.filter_prices** - Filter items by price criteria
- **jsonpath.update_prices** - Update multiple values via JSONPath
- **jsonpath.delete_isbn** - Delete fields matching JSONPath expression

### Round-Trip Benchmarks

- **roundtrip.medium_orders** - Full parse + serialize cycle

### Copy Benchmarks

- **copy.medium_object** - Deep copy of medium-sized document

## Performance Characteristics

### Parsing Performance

json.cpp achieves **2-3x faster parsing** than nlohmann/json through:

1. **Character classification lookup tables** - O(1) validation using pre-computed tables
2. **Minimal allocations** - Stack-based parsing with controlled heap usage
3. **Optimized number parsing** - Fast path for common integer cases
4. **Depth limit enforcement** - Early rejection of malicious inputs

Typical parsing throughput:
- Small documents (<1KB): ~100-150 MB/s
- Medium documents (100-500KB): ~100-110 MB/s
- Large documents (>1MB): ~100-105 MB/s

### Serialization Performance

Serialization is optimized with:

1. **Vendored double-conversion library** - Fast float/double to string conversion
2. **Float32 precision preservation** - Avoids unnecessary precision in output
3. **Minimal string copying** - Pre-allocated buffers and move semantics
4. **Escape sequence optimization** - Fast path for common printable ASCII

Typical serialization throughput:
- Compact format: ~150-165 MB/s
- Pretty-printed format: ~175-180 MB/s (paradoxically faster due to fewer escapes)

### Memory Characteristics

- **Tagged union storage** - 40 bytes per Json value (optimized for cache)
- **Lazy string allocation** - Strings stored inline when possible
- **Depth limit** - Default 20 levels prevents stack exhaustion
- **Move-optimized** - Efficient transfer of ownership for arrays/objects

### JSONPath Performance

JSONPath queries execute in linear time relative to document size:

- Simple queries ($.store.book[*].author): ~300-350 ns per query
- Filter expressions with comparisons: ~1700-1800 ns per query
- Update operations: ~2400 ns per query
- Delete operations: ~2500 ns per query

## Optimization Guidelines

### For Parsing

1. **Reuse parsed objects** when structure is similar
2. **Pre-validate JSON** at boundaries if untrusted
3. **Use structured validation** after parsing rather than during
4. **Consider streaming** for very large documents (not yet supported)

### For Serialization

1. **Use compact format** when human readability not required
2. **Pre-allocate string buffers** for known-size outputs
3. **Batch serialization** when converting multiple objects
4. **Move values** instead of copying when building JSON

### For JSONPath

1. **Cache compiled expressions** when reusing the same query
2. **Use specific paths** rather than recursive descent when possible
3. **Batch updates** rather than multiple individual updates
4. **Consider direct access** for simple field lookups

### For Construction

1. **Reserve array capacity** when size is known
2. **Build nested structures** incrementally rather than recreating
3. **Use move semantics** when transferring data into Json objects
4. **Avoid unnecessary copies** - pass by const reference when reading

## Comparison with Other Libraries

### vs nlohmann/json

| Aspect | json.cpp | nlohmann/json |
|--------|----------|---------------|
| Parse speed | **3x faster** | 1x baseline |
| Compile time | **10x faster** | 1x baseline |
| Code size | 1,536 lines | 24,766 lines |
| JSONTestSuite compliance | **Full pass** | Partial pass |
| Float32 precision | **Preserved** | Upcast to double |
| Compile dependencies | Minimal | Heavy template usage |

### Benchmark Results Snapshot

Based on recent runs with GCC 13.2 on AMD Threadripper PRO 7995WX:

```
parse.small_literal              ~5,400 ns/op  (142 MB/s)
parse.medium_orders              ~1,900,000 ns/op  (103 MB/s)
parse.large_orders               ~15,700,000 ns/op  (102 MB/s)
stringify.small_compact          ~680,000 ns/op  (164 MB/s)
stringify.large_compact          ~5,700,000 ns/op  (163 MB/s)
jsonpath.query_authors           ~340 ns/op
roundtrip.medium_orders          ~2,630,000 ns/op  (75 MB/s)
construct.empty_object           ~54 ns/op
construct.nested_object          ~120 ns/op
copy.medium_object               ~370,000 ns/op
```

## Regression Testing

To establish baseline performance and detect regressions:

1. **Generate baseline report:**
   ```bash
   ./build/json_perf --runs 20 --warmup 3 --report json > baseline.json
   ```

2. **Make code changes**

3. **Generate comparison report:**
   ```bash
   ./build/json_perf --runs 20 --warmup 3 --report json > comparison.json
   ```

4. **Compare results** using your preferred analysis tool or script

## Advanced Usage

### Running Specific Benchmarks

Filter by category:
```bash
./build/json_perf --filter parse      # Only parsing benchmarks
./build/json_perf --filter stringify  # Only serialization benchmarks
./build/json_perf --filter jsonpath   # Only JSONPath benchmarks
```

Filter by name pattern:
```bash
./build/json_perf --filter medium     # All benchmarks with "medium"
./build/json_perf --filter construct  # All construction benchmarks
```

### Adjusting Iteration Counts

For quick testing (less accurate):
```bash
./build/json_perf --runs 3 --warmup 1 --scale 0.1
```

For production profiling (more accurate, slower):
```bash
./build/json_perf --runs 100 --warmup 10 --scale 2.0
```

### Integration with CI/CD

Example GitHub Actions workflow snippet:
```yaml
- name: Run performance benchmarks
  run: |
    ./build.sh perf -- --runs 10 --report json > perf-results.json
    
- name: Upload results
  uses: actions/upload-artifact@v3
  with:
    name: performance-results
    path: perf-results.json
```

## Understanding the Metrics

### Latency Metrics

- **Mean**: Average time across all runs - affected by outliers
- **Median**: Middle value - more robust to outliers
- **Min**: Best case performance - useful for capacity planning
- **Max**: Worst case performance - important for latency SLAs
- **StdDev**: Variation in measurements - lower is more consistent
- **P95**: 95th percentile - useful for tail latency analysis
- **P99**: 99th percentile - critical for worst-case scenarios

### Throughput

Calculated as: `(bytes_per_iteration * 1e9) / median_ns`

Reported in MB/s (megabytes per second) for operations that process data.

### Iterations

The inner loop count for each benchmark run. Higher values reduce timing overhead but increase total runtime.

## Profiling and Analysis

For detailed profiling with perf:
```bash
perf record -g ./build/json_perf --runs 1 --filter parse.large
perf report
```

For CPU cache analysis:
```bash
perf stat -e cache-references,cache-misses,instructions,cycles \
  ./build/json_perf --runs 10 --filter parse
```

For memory profiling with valgrind:
```bash
valgrind --tool=massif ./build/json_perf --runs 1 --scale 0.01
ms_print massif.out.*
```

## Known Limitations

1. **Depth limit**: Default 20 levels - configurable but intentionally restricted
2. **No streaming**: Full document must fit in memory
3. **No comments**: Strict JSON only (per spec)
4. **UTF-8 validation**: Strict enforcement may reject some edge cases

## Future Optimizations

Potential areas for improvement:

- [ ] SIMD-accelerated string operations
- [ ] Memory pool for small allocations
- [ ] Streaming parser for large documents
- [ ] Parallel parsing for independent documents
- [ ] Zero-copy string views for read-only access

## Contributing Performance Improvements

When submitting performance optimizations:

1. Run benchmarks before and after changes
2. Include benchmark results in PR description
3. Verify JSONTestSuite compliance: `./build.sh test`
4. Document any trade-offs or new limitations
5. Consider both throughput and latency impacts

## Support

For performance questions or issues:
- Open an issue with benchmark results
- Include system information (CPU, compiler, flags)
- Provide reproducible test case if possible
