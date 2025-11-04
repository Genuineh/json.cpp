# Benchmark Comparison and Visualization Tools - Usage Examples

This document provides practical examples of using the automated comparison and visualization tools for json.cpp benchmarks.

## Quick Start

### 1. Generate Baseline Results

First, generate baseline performance results that you'll compare against later:

```bash
# Run comprehensive benchmarks and save as baseline
./build/json_perf --runs 20 --warmup 3 --report json > baseline.json
```

### 2. Make Code Changes

Make your performance improvements or changes to the codebase, then rebuild:

```bash
./build.sh
```

### 3. Generate Current Results

Run the same benchmarks with your changes:

```bash
./build/json_perf --runs 20 --warmup 3 --report json > current.json
```

### 4. Compare Results

#### Text Output (Terminal)

```bash
python3 benchmarks/compare_benchmarks.py baseline.json current.json
```

Output example:
```
====================================================================================================
Benchmark Comparison Report
====================================================================================================

Total Benchmarks: 24
  Improved:  8 (33.3%)
  Regressed: 2 (8.3%)
  Unchanged: 14 (58.3%)
Regression Threshold: 5.0%

----------------------------------------------------------------------------------------------------
Benchmark                                Baseline        Current         Diff            Status    
----------------------------------------------------------------------------------------------------
parse.small_literal                      5.95μs          5.73μs          -3.69%          - UNCHANGED
  → Throughput                           128.95 MB/s     133.88 MB/s     +3.82%         
parse.medium_orders                      2.00ms          1.84ms          -8.29%          ✓ IMPROVED
  → Throughput                           98.16 MB/s      107.03 MB/s     +9.04%         
...
```

#### Markdown Output (For Documentation/PRs)

```bash
python3 benchmarks/compare_benchmarks.py baseline.json current.json --format markdown > comparison.md
```

Example output in `comparison.md`:

```markdown
# Benchmark Comparison Report

## Summary

- **Total Benchmarks**: 24
- **Improved**: 8 (33.3%)
- **Regressed**: 2 (8.3%)
- **Unchanged**: 14 (58.3%)

## Detailed Results

| Benchmark | Baseline | Current | Diff | Status |
|-----------|----------|---------|------|--------|
| parse.small_literal | 5.95μs | 5.73μs | -3.69% | ➖ UNCHANGED |
| parse.medium_orders | 2.00ms | 1.84ms | -8.29% | ✅ IMPROVED |
...
```

#### JSON Output (For Further Processing)

```bash
python3 benchmarks/compare_benchmarks.py baseline.json current.json --format json > comparison.json
```

#### CSV Output (For Spreadsheet Import)

```bash
python3 benchmarks/compare_benchmarks.py baseline.json current.json --format csv > comparison.csv
```

### 5. Generate HTML Visualization

Create an interactive HTML report with charts:

```bash
python3 benchmarks/generate_html_report.py current.json --baseline baseline.json -o report.html
```

Open in browser:
```bash
# Linux
xdg-open report.html

# macOS
open report.html

# Windows
start report.html
```

## Advanced Usage

### Custom Regression Threshold

Set a custom threshold for what counts as a regression:

```bash
# 10% threshold instead of default 5%
python3 benchmarks/compare_benchmarks.py baseline.json current.json --threshold 10
```

### Sort Results

Sort by different metrics:

```bash
# Show worst regressions first
python3 benchmarks/compare_benchmarks.py baseline.json current.json --sort regression

# Show best improvements first
python3 benchmarks/compare_benchmarks.py baseline.json current.json --sort improvement

# Alphabetical by name (default)
python3 benchmarks/compare_benchmarks.py baseline.json current.json --sort name
```

### Filter Specific Benchmarks

Generate results for specific categories:

```bash
# Only parsing benchmarks
./build/json_perf --runs 10 --filter parse --report json > parse_results.json

# Only serialization benchmarks
./build/json_perf --runs 10 --filter stringify --report json > stringify_results.json

# Compare specific category
python3 benchmarks/compare_benchmarks.py parse_baseline.json parse_results.json
```

### Using Node.js Instead of Python

All comparison functionality is available in Node.js:

```bash
# Text output
node benchmarks/compare_benchmarks.js baseline.json current.json

# Markdown output
node benchmarks/compare_benchmarks.js baseline.json current.json --format markdown

# With custom options
node benchmarks/compare_benchmarks.js baseline.json current.json \
  --format json \
  --threshold 10 \
  --sort regression \
  --output comparison.json
```

### HTML Report Customization

```bash
# Custom title
python3 benchmarks/generate_html_report.py current.json \
  --title "My Performance Report" \
  --output custom_report.html

# Dark theme
python3 benchmarks/generate_html_report.py current.json \
  --theme dark \
  --output dark_report.html

# Without baseline comparison
python3 benchmarks/generate_html_report.py current.json \
  --output standalone_report.html
```

## CI/CD Integration Examples

### GitHub Actions

Create `.github/workflows/performance.yml`:

```yaml
name: Performance Benchmarks

on:
  pull_request:
    branches: [ main ]
  push:
    branches: [ main ]

jobs:
  benchmark:
    runs-on: ubuntu-latest
    
    steps:
    - uses: actions/checkout@v3
      with:
        fetch-depth: 0  # Fetch all history for baseline comparison
    
    - name: Install dependencies
      run: |
        sudo apt-get update
        sudo apt-get install -y cmake build-essential python3
    
    - name: Build baseline (main branch)
      run: |
        git checkout origin/main
        ./build.sh perf --no-test
        ./build/json_perf --runs 10 --warmup 2 --report json > baseline.json
    
    - name: Build current (PR branch)
      run: |
        git checkout ${{ github.sha }}
        ./build.sh perf --no-test
        ./build/json_perf --runs 10 --warmup 2 --report json > current.json
    
    - name: Generate comparison reports
      run: |
        python3 benchmarks/compare_benchmarks.py baseline.json current.json --format markdown > comparison.md
        python3 benchmarks/compare_benchmarks.py baseline.json current.json --format json > comparison.json
        python3 benchmarks/generate_html_report.py current.json --baseline baseline.json -o report.html
    
    - name: Upload artifacts
      uses: actions/upload-artifact@v3
      with:
        name: performance-results
        path: |
          baseline.json
          current.json
          comparison.md
          comparison.json
          report.html
    
    - name: Comment on PR
      if: github.event_name == 'pull_request'
      uses: actions/github-script@v6
      with:
        script: |
          const fs = require('fs');
          const comparison = fs.readFileSync('comparison.md', 'utf8');
          github.rest.issues.createComment({
            issue_number: context.issue.number,
            owner: context.repo.owner,
            repo: context.repo.repo,
            body: `## 📊 Performance Comparison\n\n${comparison}\n\n[View detailed HTML report in artifacts]`
          });
```

### GitLab CI

Create `.gitlab-ci.yml`:

```yaml
performance:
  stage: test
  script:
    - ./build.sh perf --no-test
    - ./build/json_perf --runs 10 --report json > current.json
    - python3 benchmarks/compare_benchmarks.py baseline.json current.json --format markdown > comparison.md
    - python3 benchmarks/generate_html_report.py current.json --baseline baseline.json -o report.html
  artifacts:
    paths:
      - current.json
      - comparison.md
      - report.html
    reports:
      metrics: comparison.json
```

## Practical Workflows

### Daily Performance Monitoring

```bash
#!/bin/bash
# daily_perf_check.sh

DATE=$(date +%Y%m%d)
BASELINE="baseline.json"
CURRENT="results_${DATE}.json"

# Run benchmarks
./build/json_perf --runs 20 --warmup 3 --report json > "$CURRENT"

# Compare with baseline
python3 benchmarks/compare_benchmarks.py "$BASELINE" "$CURRENT" \
  --format markdown > "comparison_${DATE}.md"

# Generate HTML report
python3 benchmarks/generate_html_report.py "$CURRENT" \
  --baseline "$BASELINE" \
  --output "report_${DATE}.html" \
  --title "Performance Report - $DATE"

# Check for regressions
python3 benchmarks/compare_benchmarks.py "$BASELINE" "$CURRENT" \
  --format json --sort regression | \
  python3 -c "
import sys, json
data = json.load(sys.stdin)
regressed = [c for c in data['comparisons'] if c['status'] == 'regressed']
if regressed:
    print(f'❌ {len(regressed)} regressions detected!')
    for r in regressed:
        print(f'  - {r[\"name\"]}: {r[\"diff_pct\"]:+.2f}%')
    sys.exit(1)
else:
    print('✅ No regressions detected')
"
```

### Before/After Optimization

```bash
#!/bin/bash
# optimize_and_compare.sh

# Capture before state
echo "Running baseline benchmarks..."
./build/json_perf --runs 50 --warmup 10 --report json > before.json

# Apply optimization (example: compiler flags)
echo "Applying optimization..."
# ... make changes ...
./build.sh

# Capture after state
echo "Running optimized benchmarks..."
./build/json_perf --runs 50 --warmup 10 --report json > after.json

# Compare
echo "Generating comparison..."
python3 benchmarks/compare_benchmarks.py before.json after.json \
  --format markdown --sort improvement > optimization_results.md

python3 benchmarks/generate_html_report.py after.json \
  --baseline before.json \
  --output optimization_report.html \
  --title "Optimization Results"

echo "✅ Reports generated!"
echo "View results: optimization_results.md"
echo "View visualization: optimization_report.html"
```

### Pre-commit Performance Check

```bash
#!/bin/bash
# .git/hooks/pre-commit

# Quick performance sanity check
./build.sh perf --no-test
./build/json_perf --runs 3 --warmup 1 --scale 0.5 --report json > /tmp/current.json

if [ -f baseline.json ]; then
    # Compare with baseline
    python3 benchmarks/compare_benchmarks.py baseline.json /tmp/current.json \
      --format json --threshold 15 | \
      python3 -c "
import sys, json
data = json.load(sys.stdin)
severe_regressions = [c for c in data['comparisons'] 
                     if c['status'] == 'regressed' and c['diff_pct'] > 20]
if severe_regressions:
    print('⚠️  WARNING: Severe performance regressions detected:')
    for r in severe_regressions:
        print(f'  - {r[\"name\"]}: {r[\"diff_pct\"]:+.2f}%')
    print('Consider reviewing your changes.')
    # Don't block commit, just warn
"
fi
```

## Tips and Best Practices

1. **Use consistent benchmark settings**: Always use the same `--runs` and `--warmup` values when comparing results
2. **Save baselines**: Keep baseline.json in version control for consistent comparisons
3. **Run multiple times**: For accurate results, use `--runs 20` or higher
4. **Filter wisely**: Use `--filter` to focus on relevant benchmarks during development
5. **Watch the threshold**: Default 5% threshold works for most cases, adjust based on your needs
6. **Use visualization**: HTML reports make it easier to spot patterns and trends
7. **Automate in CI**: Catch regressions early by integrating into your CI/CD pipeline
8. **Document changes**: Use markdown format for PR descriptions and documentation

## Troubleshooting

### "No baseline results found"

Make sure the baseline file exists and contains valid JSON:
```bash
# Check if file exists
ls -lh baseline.json

# Validate JSON
python3 -m json.tool baseline.json > /dev/null
```

### High variance in results

Increase warmup runs and measurement runs:
```bash
./build/json_perf --runs 100 --warmup 20 --report json > results.json
```

### Different benchmark counts

Ensure you're comparing compatible result sets. Use `--filter` to compare specific categories:
```bash
# Compare only parsing benchmarks
./build/json_perf --filter parse --report json > parse_baseline.json
./build/json_perf --filter parse --report json > parse_current.json
python3 benchmarks/compare_benchmarks.py parse_baseline.json parse_current.json
```

## See Also

- [PERFORMANCE.md](../PERFORMANCE.md) - Complete performance guide
- [README.md](../README.md) - Main project documentation
- [benchmarks/README.md](README.md) - Benchmark suite documentation
