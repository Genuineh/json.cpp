#!/usr/bin/env python3
"""
Benchmark Comparison Tool for json.cpp

Compares two JSON benchmark result files and generates a detailed comparison report
showing performance differences, regressions, and improvements.

Usage:
    python3 compare_benchmarks.py baseline.json current.json [options]

Options:
    --format FORMAT     Output format: text (default), csv, json, markdown
    --threshold PCT     Regression threshold percentage (default: 5.0)
    --output FILE       Output file (default: stdout)
    --sort METRIC       Sort by: name, improvement, regression (default: name)
"""

import json
import sys
import argparse
import csv
from typing import Dict, List, Tuple, Optional
from dataclasses import dataclass
from enum import Enum


class OutputFormat(Enum):
    TEXT = "text"
    CSV = "csv"
    JSON = "json"
    MARKDOWN = "markdown"


class SortMetric(Enum):
    NAME = "name"
    IMPROVEMENT = "improvement"
    REGRESSION = "regression"


@dataclass
class BenchmarkResult:
    """Single benchmark result"""
    name: str
    mean_ns: float
    median_ns: float
    min_ns: float
    max_ns: float
    stddev_ns: float
    p95_ns: float
    p99_ns: float
    iterations: int
    bytes_per_iteration: int
    throughput_mb_s: float


@dataclass
class Comparison:
    """Comparison between two benchmark results"""
    name: str
    baseline_median: float
    current_median: float
    diff_ns: float
    diff_pct: float
    baseline_throughput: float
    current_throughput: float
    throughput_diff_pct: float
    status: str  # "improved", "regressed", "unchanged"


def load_results(filepath: str) -> Dict[str, BenchmarkResult]:
    """Load benchmark results from JSON file"""
    try:
        with open(filepath, 'r') as f:
            data = json.load(f)
        
        results = {}
        for result in data.get('results', []):
            br = BenchmarkResult(
                name=result['name'],
                mean_ns=result['mean_ns'],
                median_ns=result['median_ns'],
                min_ns=result['min_ns'],
                max_ns=result['max_ns'],
                stddev_ns=result['stddev_ns'],
                p95_ns=result['p95_ns'],
                p99_ns=result['p99_ns'],
                iterations=result['iterations'],
                bytes_per_iteration=result.get('bytes_per_iteration', 0),
                throughput_mb_s=result.get('throughput_mb_s', 0.0)
            )
            results[br.name] = br
        
        return results
    except FileNotFoundError:
        print(f"Error: File not found: {filepath}", file=sys.stderr)
        sys.exit(1)
    except json.JSONDecodeError as e:
        print(f"Error: Invalid JSON in {filepath}: {e}", file=sys.stderr)
        sys.exit(1)
    except Exception as e:
        print(f"Error: Failed to load {filepath}: {e}", file=sys.stderr)
        sys.exit(1)


def compare_results(baseline: Dict[str, BenchmarkResult], 
                   current: Dict[str, BenchmarkResult],
                   threshold: float) -> List[Comparison]:
    """Compare baseline and current results"""
    comparisons = []
    
    # Get all benchmark names
    all_names = set(baseline.keys()) | set(current.keys())
    
    for name in sorted(all_names):
        if name not in baseline:
            # New benchmark
            curr = current[name]
            comp = Comparison(
                name=name,
                baseline_median=0.0,
                current_median=curr.median_ns,
                diff_ns=curr.median_ns,
                diff_pct=float('inf'),
                baseline_throughput=0.0,
                current_throughput=curr.throughput_mb_s,
                throughput_diff_pct=float('inf'),
                status="new"
            )
            comparisons.append(comp)
        elif name not in current:
            # Removed benchmark
            base = baseline[name]
            comp = Comparison(
                name=name,
                baseline_median=base.median_ns,
                current_median=0.0,
                diff_ns=-base.median_ns,
                diff_pct=float('-inf'),
                baseline_throughput=base.throughput_mb_s,
                current_throughput=0.0,
                throughput_diff_pct=float('-inf'),
                status="removed"
            )
            comparisons.append(comp)
        else:
            # Compare existing benchmarks
            base = baseline[name]
            curr = current[name]
            
            diff_ns = curr.median_ns - base.median_ns
            diff_pct = (diff_ns / base.median_ns * 100) if base.median_ns > 0 else 0.0
            
            throughput_diff_pct = 0.0
            if base.throughput_mb_s > 0 and curr.throughput_mb_s > 0:
                throughput_diff_pct = ((curr.throughput_mb_s - base.throughput_mb_s) 
                                      / base.throughput_mb_s * 100)
            
            # Determine status (note: lower time is better, so negative diff is improvement)
            if abs(diff_pct) < threshold:
                status = "unchanged"
            elif diff_pct < 0:
                status = "improved"
            else:
                status = "regressed"
            
            comp = Comparison(
                name=name,
                baseline_median=base.median_ns,
                current_median=curr.median_ns,
                diff_ns=diff_ns,
                diff_pct=diff_pct,
                baseline_throughput=base.throughput_mb_s,
                current_throughput=curr.throughput_mb_s,
                throughput_diff_pct=throughput_diff_pct,
                status=status
            )
            comparisons.append(comp)
    
    return comparisons


def sort_comparisons(comparisons: List[Comparison], metric: SortMetric) -> List[Comparison]:
    """Sort comparisons by specified metric"""
    if metric == SortMetric.NAME:
        return sorted(comparisons, key=lambda c: c.name)
    elif metric == SortMetric.IMPROVEMENT:
        return sorted(comparisons, key=lambda c: c.diff_pct)
    elif metric == SortMetric.REGRESSION:
        return sorted(comparisons, key=lambda c: c.diff_pct, reverse=True)
    return comparisons


def format_ns(ns: float) -> str:
    """Format nanoseconds in human-readable form"""
    if ns == 0:
        return "N/A"
    elif ns >= 1_000_000_000:
        return f"{ns/1_000_000_000:.2f}s"
    elif ns >= 1_000_000:
        return f"{ns/1_000_000:.2f}ms"
    elif ns >= 1_000:
        return f"{ns/1_000:.2f}μs"
    else:
        return f"{ns:.2f}ns"


def format_throughput(mb_s: float) -> str:
    """Format throughput"""
    if mb_s == 0:
        return "N/A"
    return f"{mb_s:.2f} MB/s"


def output_text(comparisons: List[Comparison], threshold: float) -> str:
    """Generate text format output"""
    lines = []
    lines.append("=" * 100)
    lines.append("Benchmark Comparison Report")
    lines.append("=" * 100)
    lines.append("")
    
    # Summary statistics
    total = len(comparisons)
    improved = sum(1 for c in comparisons if c.status == "improved")
    regressed = sum(1 for c in comparisons if c.status == "regressed")
    unchanged = sum(1 for c in comparisons if c.status == "unchanged")
    new = sum(1 for c in comparisons if c.status == "new")
    removed = sum(1 for c in comparisons if c.status == "removed")
    
    lines.append(f"Total Benchmarks: {total}")
    lines.append(f"  Improved:  {improved} ({improved/total*100:.1f}%)")
    lines.append(f"  Regressed: {regressed} ({regressed/total*100:.1f}%)")
    lines.append(f"  Unchanged: {unchanged} ({unchanged/total*100:.1f}%)")
    if new > 0:
        lines.append(f"  New:       {new}")
    if removed > 0:
        lines.append(f"  Removed:   {removed}")
    lines.append(f"Regression Threshold: {threshold}%")
    lines.append("")
    lines.append("-" * 100)
    
    # Header
    lines.append(f"{'Benchmark':<40} {'Baseline':<15} {'Current':<15} {'Diff':<15} {'Status':<10}")
    lines.append("-" * 100)
    
    # Results
    for comp in comparisons:
        status_symbol = {
            "improved": "✓ IMPROVED",
            "regressed": "✗ REGRESSED",
            "unchanged": "- UNCHANGED",
            "new": "+ NEW",
            "removed": "- REMOVED"
        }.get(comp.status, comp.status)
        
        diff_str = f"{comp.diff_pct:+.2f}%" if abs(comp.diff_pct) != float('inf') else "N/A"
        
        lines.append(
            f"{comp.name:<40} "
            f"{format_ns(comp.baseline_median):<15} "
            f"{format_ns(comp.current_median):<15} "
            f"{diff_str:<15} "
            f"{status_symbol:<10}"
        )
        
        # Add throughput info if available
        if comp.baseline_throughput > 0 or comp.current_throughput > 0:
            tp_diff = f"{comp.throughput_diff_pct:+.2f}%" if abs(comp.throughput_diff_pct) != float('inf') else "N/A"
            lines.append(
                f"{'  → Throughput':<40} "
                f"{format_throughput(comp.baseline_throughput):<15} "
                f"{format_throughput(comp.current_throughput):<15} "
                f"{tp_diff:<15}"
            )
    
    lines.append("=" * 100)
    return "\n".join(lines)


def output_csv(comparisons: List[Comparison]) -> str:
    """Generate CSV format output"""
    import io
    output = io.StringIO()
    writer = csv.writer(output)
    
    # Header
    writer.writerow([
        'Benchmark',
        'Baseline (ns)',
        'Current (ns)',
        'Diff (ns)',
        'Diff (%)',
        'Baseline Throughput (MB/s)',
        'Current Throughput (MB/s)',
        'Throughput Diff (%)',
        'Status'
    ])
    
    # Data
    for comp in comparisons:
        writer.writerow([
            comp.name,
            comp.baseline_median,
            comp.current_median,
            comp.diff_ns,
            comp.diff_pct,
            comp.baseline_throughput,
            comp.current_throughput,
            comp.throughput_diff_pct,
            comp.status
        ])
    
    return output.getvalue()


def output_json(comparisons: List[Comparison], threshold: float) -> str:
    """Generate JSON format output"""
    total = len(comparisons)
    result = {
        "summary": {
            "total": total,
            "improved": sum(1 for c in comparisons if c.status == "improved"),
            "regressed": sum(1 for c in comparisons if c.status == "regressed"),
            "unchanged": sum(1 for c in comparisons if c.status == "unchanged"),
            "new": sum(1 for c in comparisons if c.status == "new"),
            "removed": sum(1 for c in comparisons if c.status == "removed"),
            "threshold": threshold
        },
        "comparisons": [
            {
                "name": c.name,
                "baseline_median_ns": c.baseline_median,
                "current_median_ns": c.current_median,
                "diff_ns": c.diff_ns,
                "diff_pct": c.diff_pct,
                "baseline_throughput_mb_s": c.baseline_throughput,
                "current_throughput_mb_s": c.current_throughput,
                "throughput_diff_pct": c.throughput_diff_pct,
                "status": c.status
            }
            for c in comparisons
        ]
    }
    return json.dumps(result, indent=2)


def output_markdown(comparisons: List[Comparison], threshold: float) -> str:
    """Generate Markdown format output"""
    lines = []
    lines.append("# Benchmark Comparison Report")
    lines.append("")
    
    # Summary
    total = len(comparisons)
    improved = sum(1 for c in comparisons if c.status == "improved")
    regressed = sum(1 for c in comparisons if c.status == "regressed")
    unchanged = sum(1 for c in comparisons if c.status == "unchanged")
    new = sum(1 for c in comparisons if c.status == "new")
    removed = sum(1 for c in comparisons if c.status == "removed")
    
    lines.append("## Summary")
    lines.append("")
    lines.append(f"- **Total Benchmarks**: {total}")
    lines.append(f"- **Improved**: {improved} ({improved/total*100:.1f}%)")
    lines.append(f"- **Regressed**: {regressed} ({regressed/total*100:.1f}%)")
    lines.append(f"- **Unchanged**: {unchanged} ({unchanged/total*100:.1f}%)")
    if new > 0:
        lines.append(f"- **New**: {new}")
    if removed > 0:
        lines.append(f"- **Removed**: {removed}")
    lines.append(f"- **Regression Threshold**: {threshold}%")
    lines.append("")
    
    # Detailed results
    lines.append("## Detailed Results")
    lines.append("")
    lines.append("| Benchmark | Baseline | Current | Diff | Status |")
    lines.append("|-----------|----------|---------|------|--------|")
    
    for comp in comparisons:
        status_emoji = {
            "improved": "✅",
            "regressed": "❌",
            "unchanged": "➖",
            "new": "🆕",
            "removed": "🗑️"
        }.get(comp.status, "")
        
        diff_str = f"{comp.diff_pct:+.2f}%" if abs(comp.diff_pct) != float('inf') else "N/A"
        
        lines.append(
            f"| {comp.name} | "
            f"{format_ns(comp.baseline_median)} | "
            f"{format_ns(comp.current_median)} | "
            f"{diff_str} | "
            f"{status_emoji} {comp.status.upper()} |"
        )
    
    lines.append("")
    
    # Throughput table if available
    has_throughput = any(c.baseline_throughput > 0 or c.current_throughput > 0 for c in comparisons)
    if has_throughput:
        lines.append("## Throughput Comparison")
        lines.append("")
        lines.append("| Benchmark | Baseline | Current | Diff |")
        lines.append("|-----------|----------|---------|------|")
        
        for comp in comparisons:
            if comp.baseline_throughput > 0 or comp.current_throughput > 0:
                tp_diff = f"{comp.throughput_diff_pct:+.2f}%" if abs(comp.throughput_diff_pct) != float('inf') else "N/A"
                lines.append(
                    f"| {comp.name} | "
                    f"{format_throughput(comp.baseline_throughput)} | "
                    f"{format_throughput(comp.current_throughput)} | "
                    f"{tp_diff} |"
                )
        lines.append("")
    
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(
        description="Compare two JSON benchmark result files",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__
    )
    parser.add_argument("baseline", help="Baseline benchmark JSON file")
    parser.add_argument("current", help="Current benchmark JSON file")
    parser.add_argument("--format", choices=["text", "csv", "json", "markdown"],
                       default="text", help="Output format (default: text)")
    parser.add_argument("--threshold", type=float, default=5.0,
                       help="Regression threshold percentage (default: 5.0)")
    parser.add_argument("--output", "-o", help="Output file (default: stdout)")
    parser.add_argument("--sort", choices=["name", "improvement", "regression"],
                       default="name", help="Sort results by (default: name)")
    
    args = parser.parse_args()
    
    # Load results
    baseline = load_results(args.baseline)
    current = load_results(args.current)
    
    if not baseline:
        print("Error: No baseline results found", file=sys.stderr)
        sys.exit(1)
    
    if not current:
        print("Error: No current results found", file=sys.stderr)
        sys.exit(1)
    
    # Compare
    comparisons = compare_results(baseline, current, args.threshold)
    
    # Sort
    sort_metric = SortMetric(args.sort)
    comparisons = sort_comparisons(comparisons, sort_metric)
    
    # Generate output
    if args.format == "text":
        output = output_text(comparisons, args.threshold)
    elif args.format == "csv":
        output = output_csv(comparisons)
    elif args.format == "json":
        output = output_json(comparisons, args.threshold)
    elif args.format == "markdown":
        output = output_markdown(comparisons, args.threshold)
    else:
        print(f"Error: Unknown format: {args.format}", file=sys.stderr)
        sys.exit(1)
    
    # Write output
    if args.output:
        try:
            with open(args.output, 'w') as f:
                f.write(output)
            print(f"Report written to: {args.output}", file=sys.stderr)
        except Exception as e:
            print(f"Error writing to {args.output}: {e}", file=sys.stderr)
            sys.exit(1)
    else:
        print(output)


if __name__ == "__main__":
    main()
