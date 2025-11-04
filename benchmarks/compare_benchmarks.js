#!/usr/bin/env node
/**
 * Benchmark Comparison Tool for json.cpp (Node.js version)
 * 
 * Compares two JSON benchmark result files and generates a detailed comparison report
 * showing performance differences, regressions, and improvements.
 * 
 * Usage:
 *     node compare_benchmarks.js baseline.json current.json [options]
 * 
 * Options:
 *     --format FORMAT     Output format: text (default), csv, json, markdown
 *     --threshold PCT     Regression threshold percentage (default: 5.0)
 *     --output FILE       Output file (default: stdout)
 *     --sort METRIC       Sort by: name, improvement, regression (default: name)
 */

const fs = require('fs');
const path = require('path');

class BenchmarkResult {
    constructor(data) {
        this.name = data.name;
        this.mean_ns = data.mean_ns;
        this.median_ns = data.median_ns;
        this.min_ns = data.min_ns;
        this.max_ns = data.max_ns;
        this.stddev_ns = data.stddev_ns;
        this.p95_ns = data.p95_ns;
        this.p99_ns = data.p99_ns;
        this.iterations = data.iterations;
        this.bytes_per_iteration = data.bytes_per_iteration || 0;
        this.throughput_mb_s = data.throughput_mb_s || 0.0;
    }
}

class Comparison {
    constructor(name, baseline_median, current_median, diff_ns, diff_pct,
                baseline_throughput, current_throughput, throughput_diff_pct, status) {
        this.name = name;
        this.baseline_median = baseline_median;
        this.current_median = current_median;
        this.diff_ns = diff_ns;
        this.diff_pct = diff_pct;
        this.baseline_throughput = baseline_throughput;
        this.current_throughput = current_throughput;
        this.throughput_diff_pct = throughput_diff_pct;
        this.status = status;
    }
}

function loadResults(filepath) {
    try {
        const data = JSON.parse(fs.readFileSync(filepath, 'utf8'));
        const results = {};
        
        for (const result of data.results || []) {
            const br = new BenchmarkResult(result);
            results[br.name] = br;
        }
        
        return results;
    } catch (err) {
        if (err.code === 'ENOENT') {
            console.error(`Error: File not found: ${filepath}`);
        } else if (err instanceof SyntaxError) {
            console.error(`Error: Invalid JSON in ${filepath}: ${err.message}`);
        } else {
            console.error(`Error: Failed to load ${filepath}: ${err.message}`);
        }
        process.exit(1);
    }
}

function compareResults(baseline, current, threshold) {
    const comparisons = [];
    const allNames = new Set([...Object.keys(baseline), ...Object.keys(current)]);
    
    for (const name of Array.from(allNames).sort()) {
        if (!(name in baseline)) {
            // New benchmark
            const curr = current[name];
            comparisons.push(new Comparison(
                name,
                0.0,
                curr.median_ns,
                curr.median_ns,
                Infinity,
                0.0,
                curr.throughput_mb_s,
                Infinity,
                "new"
            ));
        } else if (!(name in current)) {
            // Removed benchmark
            const base = baseline[name];
            comparisons.push(new Comparison(
                name,
                base.median_ns,
                0.0,
                -base.median_ns,
                -Infinity,
                base.throughput_mb_s,
                0.0,
                -Infinity,
                "removed"
            ));
        } else {
            // Compare existing benchmarks
            const base = baseline[name];
            const curr = current[name];
            
            const diff_ns = curr.median_ns - base.median_ns;
            const diff_pct = base.median_ns > 0 ? (diff_ns / base.median_ns * 100) : 0.0;
            
            let throughput_diff_pct = 0.0;
            if (base.throughput_mb_s > 0 && curr.throughput_mb_s > 0) {
                throughput_diff_pct = ((curr.throughput_mb_s - base.throughput_mb_s) 
                                      / base.throughput_mb_s * 100);
            }
            
            // Determine status (note: lower time is better, so negative diff is improvement)
            let status;
            if (Math.abs(diff_pct) < threshold) {
                status = "unchanged";
            } else if (diff_pct < 0) {
                status = "improved";
            } else {
                status = "regressed";
            }
            
            comparisons.push(new Comparison(
                name,
                base.median_ns,
                curr.median_ns,
                diff_ns,
                diff_pct,
                base.throughput_mb_s,
                curr.throughput_mb_s,
                throughput_diff_pct,
                status
            ));
        }
    }
    
    return comparisons;
}

function sortComparisons(comparisons, metric) {
    if (metric === "name") {
        return comparisons.sort((a, b) => a.name.localeCompare(b.name));
    } else if (metric === "improvement") {
        return comparisons.sort((a, b) => a.diff_pct - b.diff_pct);
    } else if (metric === "regression") {
        return comparisons.sort((a, b) => b.diff_pct - a.diff_pct);
    }
    return comparisons;
}

function formatNs(ns) {
    if (ns === 0) return "N/A";
    if (ns >= 1_000_000_000) return `${(ns/1_000_000_000).toFixed(2)}s`;
    if (ns >= 1_000_000) return `${(ns/1_000_000).toFixed(2)}ms`;
    if (ns >= 1_000) return `${(ns/1_000).toFixed(2)}μs`;
    return `${ns.toFixed(2)}ns`;
}

function formatThroughput(mb_s) {
    if (mb_s === 0) return "N/A";
    return `${mb_s.toFixed(2)} MB/s`;
}

function outputText(comparisons, threshold) {
    const lines = [];
    const separator = "=".repeat(100);
    const divider = "-".repeat(100);
    
    lines.push(separator);
    lines.push("Benchmark Comparison Report");
    lines.push(separator);
    lines.push("");
    
    // Summary statistics
    const total = comparisons.length;
    const improved = comparisons.filter(c => c.status === "improved").length;
    const regressed = comparisons.filter(c => c.status === "regressed").length;
    const unchanged = comparisons.filter(c => c.status === "unchanged").length;
    const newCount = comparisons.filter(c => c.status === "new").length;
    const removed = comparisons.filter(c => c.status === "removed").length;
    
    lines.push(`Total Benchmarks: ${total}`);
    lines.push(`  Improved:  ${improved} (${(improved/total*100).toFixed(1)}%)`);
    lines.push(`  Regressed: ${regressed} (${(regressed/total*100).toFixed(1)}%)`);
    lines.push(`  Unchanged: ${unchanged} (${(unchanged/total*100).toFixed(1)}%)`);
    if (newCount > 0) lines.push(`  New:       ${newCount}`);
    if (removed > 0) lines.push(`  Removed:   ${removed}`);
    lines.push(`Regression Threshold: ${threshold}%`);
    lines.push("");
    lines.push(divider);
    
    // Header
    lines.push(
        "Benchmark".padEnd(40) +
        "Baseline".padEnd(15) +
        "Current".padEnd(15) +
        "Diff".padEnd(15) +
        "Status".padEnd(10)
    );
    lines.push(divider);
    
    // Results
    for (const comp of comparisons) {
        const statusSymbol = {
            "improved": "✓ IMPROVED",
            "regressed": "✗ REGRESSED",
            "unchanged": "- UNCHANGED",
            "new": "+ NEW",
            "removed": "- REMOVED"
        }[comp.status] || comp.status;
        
        const diff_str = (!isFinite(comp.diff_pct)) ? "N/A" : `${comp.diff_pct > 0 ? '+' : ''}${comp.diff_pct.toFixed(2)}%`;
        
        lines.push(
            comp.name.padEnd(40) +
            formatNs(comp.baseline_median).padEnd(15) +
            formatNs(comp.current_median).padEnd(15) +
            diff_str.padEnd(15) +
            statusSymbol.padEnd(10)
        );
        
        // Add throughput info if available
        if (comp.baseline_throughput > 0 || comp.current_throughput > 0) {
            const tp_diff = (!isFinite(comp.throughput_diff_pct)) ? 
                "N/A" : 
                `${comp.throughput_diff_pct > 0 ? '+' : ''}${comp.throughput_diff_pct.toFixed(2)}%`;
            
            lines.push(
                "  → Throughput".padEnd(40) +
                formatThroughput(comp.baseline_throughput).padEnd(15) +
                formatThroughput(comp.current_throughput).padEnd(15) +
                tp_diff.padEnd(15)
            );
        }
    }
    
    lines.push(separator);
    return lines.join('\n');
}

function outputCsv(comparisons) {
    const lines = [];
    
    // Header
    lines.push([
        'Benchmark',
        'Baseline (ns)',
        'Current (ns)',
        'Diff (ns)',
        'Diff (%)',
        'Baseline Throughput (MB/s)',
        'Current Throughput (MB/s)',
        'Throughput Diff (%)',
        'Status'
    ].join(','));
    
    // Data
    for (const comp of comparisons) {
        lines.push([
            `"${comp.name}"`,
            comp.baseline_median,
            comp.current_median,
            comp.diff_ns,
            comp.diff_pct,
            comp.baseline_throughput,
            comp.current_throughput,
            comp.throughput_diff_pct,
            comp.status
        ].join(','));
    }
    
    return lines.join('\n');
}

function outputJson(comparisons, threshold) {
    const total = comparisons.length;
    const result = {
        summary: {
            total: total,
            improved: comparisons.filter(c => c.status === "improved").length,
            regressed: comparisons.filter(c => c.status === "regressed").length,
            unchanged: comparisons.filter(c => c.status === "unchanged").length,
            new: comparisons.filter(c => c.status === "new").length,
            removed: comparisons.filter(c => c.status === "removed").length,
            threshold: threshold
        },
        comparisons: comparisons.map(c => ({
            name: c.name,
            baseline_median_ns: c.baseline_median,
            current_median_ns: c.current_median,
            diff_ns: c.diff_ns,
            diff_pct: c.diff_pct,
            baseline_throughput_mb_s: c.baseline_throughput,
            current_throughput_mb_s: c.current_throughput,
            throughput_diff_pct: c.throughput_diff_pct,
            status: c.status
        }))
    };
    return JSON.stringify(result, null, 2);
}

function outputMarkdown(comparisons, threshold) {
    const lines = [];
    lines.push("# Benchmark Comparison Report");
    lines.push("");
    
    // Summary
    const total = comparisons.length;
    const improved = comparisons.filter(c => c.status === "improved").length;
    const regressed = comparisons.filter(c => c.status === "regressed").length;
    const unchanged = comparisons.filter(c => c.status === "unchanged").length;
    const newCount = comparisons.filter(c => c.status === "new").length;
    const removed = comparisons.filter(c => c.status === "removed").length;
    
    lines.push("## Summary");
    lines.push("");
    lines.push(`- **Total Benchmarks**: ${total}`);
    lines.push(`- **Improved**: ${improved} (${(improved/total*100).toFixed(1)}%)`);
    lines.push(`- **Regressed**: ${regressed} (${(regressed/total*100).toFixed(1)}%)`);
    lines.push(`- **Unchanged**: ${unchanged} (${(unchanged/total*100).toFixed(1)}%)`);
    if (newCount > 0) lines.push(`- **New**: ${newCount}`);
    if (removed > 0) lines.push(`- **Removed**: ${removed}`);
    lines.push(`- **Regression Threshold**: ${threshold}%`);
    lines.push("");
    
    // Detailed results
    lines.push("## Detailed Results");
    lines.push("");
    lines.push("| Benchmark | Baseline | Current | Diff | Status |");
    lines.push("|-----------|----------|---------|------|--------|");
    
    for (const comp of comparisons) {
        const statusEmoji = {
            "improved": "✅",
            "regressed": "❌",
            "unchanged": "➖",
            "new": "🆕",
            "removed": "🗑️"
        }[comp.status] || "";
        
        const diff_str = (!isFinite(comp.diff_pct)) ? 
            "N/A" : 
            `${comp.diff_pct > 0 ? '+' : ''}${comp.diff_pct.toFixed(2)}%`;
        
        lines.push(
            `| ${comp.name} | ` +
            `${formatNs(comp.baseline_median)} | ` +
            `${formatNs(comp.current_median)} | ` +
            `${diff_str} | ` +
            `${statusEmoji} ${comp.status.toUpperCase()} |`
        );
    }
    
    lines.push("");
    
    // Throughput table if available
    const hasThroughput = comparisons.some(c => c.baseline_throughput > 0 || c.current_throughput > 0);
    if (hasThroughput) {
        lines.push("## Throughput Comparison");
        lines.push("");
        lines.push("| Benchmark | Baseline | Current | Diff |");
        lines.push("|-----------|----------|---------|------|");
        
        for (const comp of comparisons) {
            if (comp.baseline_throughput > 0 || comp.current_throughput > 0) {
                const tp_diff = (!isFinite(comp.throughput_diff_pct)) ? 
                    "N/A" : 
                    `${comp.throughput_diff_pct > 0 ? '+' : ''}${comp.throughput_diff_pct.toFixed(2)}%`;
                
                lines.push(
                    `| ${comp.name} | ` +
                    `${formatThroughput(comp.baseline_throughput)} | ` +
                    `${formatThroughput(comp.current_throughput)} | ` +
                    `${tp_diff} |`
                );
            }
        }
        lines.push("");
    }
    
    return lines.join('\n');
}

function printUsage() {
    console.log(`
Benchmark Comparison Tool for json.cpp (Node.js version)

Usage:
    node compare_benchmarks.js baseline.json current.json [options]

Options:
    --format FORMAT     Output format: text (default), csv, json, markdown
    --threshold PCT     Regression threshold percentage (default: 5.0)
    --output FILE       Output file (default: stdout)
    --sort METRIC       Sort by: name, improvement, regression (default: name)
    --help              Show this help message

Examples:
    node compare_benchmarks.js baseline.json current.json
    node compare_benchmarks.js baseline.json current.json --format markdown
    node compare_benchmarks.js baseline.json current.json --threshold 10 --sort regression
    node compare_benchmarks.js baseline.json current.json --format json --output report.json
`);
}

function parseArgs() {
    const args = process.argv.slice(2);
    const options = {
        baseline: null,
        current: null,
        format: 'text',
        threshold: 5.0,
        output: null,
        sort: 'name'
    };
    
    for (let i = 0; i < args.length; i++) {
        const arg = args[i];
        
        if (arg === '--help' || arg === '-h') {
            printUsage();
            process.exit(0);
        } else if (arg === '--format') {
            options.format = args[++i];
        } else if (arg === '--threshold') {
            options.threshold = parseFloat(args[++i]);
        } else if (arg === '--output' || arg === '-o') {
            options.output = args[++i];
        } else if (arg === '--sort') {
            options.sort = args[++i];
        } else if (!options.baseline) {
            options.baseline = arg;
        } else if (!options.current) {
            options.current = arg;
        } else {
            console.error(`Error: Unknown argument: ${arg}`);
            printUsage();
            process.exit(1);
        }
    }
    
    if (!options.baseline || !options.current) {
        console.error('Error: Both baseline and current files are required');
        printUsage();
        process.exit(1);
    }
    
    return options;
}

function main() {
    const options = parseArgs();
    
    // Load results
    const baseline = loadResults(options.baseline);
    const current = loadResults(options.current);
    
    if (Object.keys(baseline).length === 0) {
        console.error('Error: No baseline results found');
        process.exit(1);
    }
    
    if (Object.keys(current).length === 0) {
        console.error('Error: No current results found');
        process.exit(1);
    }
    
    // Compare
    let comparisons = compareResults(baseline, current, options.threshold);
    
    // Sort
    comparisons = sortComparisons(comparisons, options.sort);
    
    // Generate output
    let output;
    if (options.format === 'text') {
        output = outputText(comparisons, options.threshold);
    } else if (options.format === 'csv') {
        output = outputCsv(comparisons);
    } else if (options.format === 'json') {
        output = outputJson(comparisons, options.threshold);
    } else if (options.format === 'markdown') {
        output = outputMarkdown(comparisons, options.threshold);
    } else {
        console.error(`Error: Unknown format: ${options.format}`);
        process.exit(1);
    }
    
    // Write output
    if (options.output) {
        try {
            fs.writeFileSync(options.output, output);
            console.error(`Report written to: ${options.output}`);
        } catch (err) {
            console.error(`Error writing to ${options.output}: ${err.message}`);
            process.exit(1);
        }
    } else {
        console.log(output);
    }
}

if (require.main === module) {
    main();
}
