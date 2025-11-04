#!/usr/bin/env python3
"""
HTML Visualization Report Generator for json.cpp

Generates interactive HTML reports with charts visualizing benchmark results.
Uses Chart.js for visualization (loaded from CDN).

Usage:
    python3 generate_html_report.py results.json [options]

Options:
    --output FILE       Output HTML file (default: benchmark_report.html)
    --title TITLE       Report title (default: "json.cpp Benchmark Report")
    --baseline FILE     Baseline JSON file for comparison
    --theme THEME       Color theme: light, dark (default: light)
"""

import json
import sys
import argparse
from datetime import datetime
from typing import Dict, List, Optional


def load_results(filepath: str) -> dict:
    """Load benchmark results from JSON file"""
    try:
        with open(filepath, 'r') as f:
            return json.load(f)
    except FileNotFoundError:
        print(f"Error: File not found: {filepath}", file=sys.stderr)
        sys.exit(1)
    except json.JSONDecodeError as e:
        print(f"Error: Invalid JSON in {filepath}: {e}", file=sys.stderr)
        sys.exit(1)


def format_ns(ns: float) -> str:
    """Format nanoseconds in human-readable form"""
    if ns >= 1_000_000_000:
        return f"{ns/1_000_000_000:.2f}s"
    elif ns >= 1_000_000:
        return f"{ns/1_000_000:.2f}ms"
    elif ns >= 1_000:
        return f"{ns/1_000:.2f}μs"
    else:
        return f"{ns:.2f}ns"


def generate_html(results_data: dict, baseline_data: Optional[dict], 
                 title: str, theme: str) -> str:
    """Generate HTML report with charts"""
    
    results = results_data.get('results', [])
    config = results_data.get('config', {})
    
    # Prepare data for charts
    benchmark_names = [r['name'] for r in results]
    median_times = [r['median_ns'] for r in results]
    mean_times = [r['mean_ns'] for r in results]
    min_times = [r['min_ns'] for r in results]
    max_times = [r['max_ns'] for r in results]
    p95_times = [r['p95_ns'] for r in results]
    throughputs = [r.get('throughput_mb_s', 0) for r in results]
    
    # Comparison data if baseline provided
    comparison_data = []
    if baseline_data:
        baseline_results = {r['name']: r for r in baseline_data.get('results', [])}
        for r in results:
            name = r['name']
            if name in baseline_results:
                baseline_median = baseline_results[name]['median_ns']
                current_median = r['median_ns']
                diff_pct = ((current_median - baseline_median) / baseline_median * 100) if baseline_median > 0 else 0
                comparison_data.append({
                    'name': name,
                    'baseline': baseline_median,
                    'current': current_median,
                    'diff_pct': diff_pct
                })
    
    # Theme colors
    if theme == 'dark':
        bg_color = '#1a1a1a'
        text_color = '#e0e0e0'
        grid_color = '#444'
        card_bg = '#2a2a2a'
    else:
        bg_color = '#ffffff'
        text_color = '#333333'
        grid_color = '#e0e0e0'
        card_bg = '#f8f9fa'
    
    # Generate HTML
    html = f"""<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>{title}</title>
    <script src="https://cdn.jsdelivr.net/npm/chart.js@4.4.0/dist/chart.umd.min.js"></script>
    <style>
        * {{
            margin: 0;
            padding: 0;
            box-sizing: border-box;
        }}
        
        body {{
            font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, 'Helvetica Neue', Arial, sans-serif;
            background-color: {bg_color};
            color: {text_color};
            padding: 20px;
            line-height: 1.6;
        }}
        
        .container {{
            max-width: 1400px;
            margin: 0 auto;
        }}
        
        header {{
            text-align: center;
            margin-bottom: 40px;
            padding: 30px 0;
            border-bottom: 2px solid {grid_color};
        }}
        
        h1 {{
            font-size: 2.5em;
            margin-bottom: 10px;
        }}
        
        .subtitle {{
            color: #666;
            font-size: 1.1em;
        }}
        
        .config-info {{
            background: {card_bg};
            padding: 20px;
            border-radius: 8px;
            margin-bottom: 30px;
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
            gap: 15px;
        }}
        
        .config-item {{
            padding: 10px;
        }}
        
        .config-label {{
            font-weight: bold;
            color: #666;
            font-size: 0.9em;
        }}
        
        .config-value {{
            font-size: 1.2em;
            margin-top: 5px;
        }}
        
        .chart-container {{
            background: {card_bg};
            padding: 25px;
            border-radius: 8px;
            margin-bottom: 30px;
            box-shadow: 0 2px 4px rgba(0,0,0,0.1);
        }}
        
        .chart-title {{
            font-size: 1.5em;
            margin-bottom: 20px;
            font-weight: 600;
        }}
        
        canvas {{
            max-height: 500px;
        }}
        
        .stats-table {{
            width: 100%;
            background: {card_bg};
            border-radius: 8px;
            overflow: hidden;
            margin-bottom: 30px;
        }}
        
        table {{
            width: 100%;
            border-collapse: collapse;
        }}
        
        th {{
            background: #007bff;
            color: white;
            padding: 15px;
            text-align: left;
            font-weight: 600;
        }}
        
        td {{
            padding: 12px 15px;
            border-bottom: 1px solid {grid_color};
        }}
        
        tr:hover {{
            background: rgba(0, 123, 255, 0.05);
        }}
        
        .metric {{
            font-family: 'Courier New', monospace;
            font-size: 0.95em;
        }}
        
        .improved {{
            color: #28a745;
            font-weight: bold;
        }}
        
        .regressed {{
            color: #dc3545;
            font-weight: bold;
        }}
        
        .footer {{
            text-align: center;
            margin-top: 50px;
            padding-top: 20px;
            border-top: 1px solid {grid_color};
            color: #666;
            font-size: 0.9em;
        }}
        
        @media print {{
            .chart-container {{
                page-break-inside: avoid;
            }}
        }}
    </style>
</head>
<body>
    <div class="container">
        <header>
            <h1>{title}</h1>
            <p class="subtitle">Generated on {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}</p>
        </header>
        
        <div class="config-info">
            <div class="config-item">
                <div class="config-label">Total Benchmarks</div>
                <div class="config-value">{len(results)}</div>
            </div>
            <div class="config-item">
                <div class="config-label">Warmup Runs</div>
                <div class="config-value">{config.get('warmup_runs', 'N/A')}</div>
            </div>
            <div class="config-item">
                <div class="config-label">Measure Runs</div>
                <div class="config-value">{config.get('measure_runs', 'N/A')}</div>
            </div>
            <div class="config-item">
                <div class="config-label">Scale Factor</div>
                <div class="config-value">{config.get('scale', 'N/A')}</div>
            </div>
        </div>
"""
    
    # Add comparison chart if baseline provided
    if comparison_data:
        html += f"""
        <div class="chart-container">
            <h2 class="chart-title">📊 Performance Comparison (Baseline vs Current)</h2>
            <canvas id="comparisonChart"></canvas>
        </div>
"""
    
    # Add main charts
    html += f"""
        <div class="chart-container">
            <h2 class="chart-title">⏱️ Median Execution Time by Benchmark</h2>
            <canvas id="medianChart"></canvas>
        </div>
        
        <div class="chart-container">
            <h2 class="chart-title">📈 Performance Metrics Distribution</h2>
            <canvas id="distributionChart"></canvas>
        </div>
"""
    
    # Add throughput chart if any benchmarks have throughput data
    if any(t > 0 for t in throughputs):
        html += f"""
        <div class="chart-container">
            <h2 class="chart-title">🚀 Throughput (MB/s)</h2>
            <canvas id="throughputChart"></canvas>
        </div>
"""
    
    # Add detailed statistics table
    html += f"""
        <div class="stats-table">
            <h2 class="chart-title" style="padding: 20px 20px 0 20px;">📋 Detailed Statistics</h2>
            <table>
                <thead>
                    <tr>
                        <th>Benchmark</th>
                        <th>Median</th>
                        <th>Mean</th>
                        <th>Min</th>
                        <th>Max</th>
                        <th>P95</th>
                        <th>Throughput</th>
"""
    
    if comparison_data:
        html += """
                        <th>vs Baseline</th>
"""
    
    html += """
                    </tr>
                </thead>
                <tbody>
"""
    
    # Add table rows
    comparison_map = {c['name']: c for c in comparison_data}
    for r in results:
        throughput_str = f"{r.get('throughput_mb_s', 0):.2f} MB/s" if r.get('throughput_mb_s', 0) > 0 else "N/A"
        
        html += f"""
                    <tr>
                        <td><strong>{r['name']}</strong></td>
                        <td class="metric">{format_ns(r['median_ns'])}</td>
                        <td class="metric">{format_ns(r['mean_ns'])}</td>
                        <td class="metric">{format_ns(r['min_ns'])}</td>
                        <td class="metric">{format_ns(r['max_ns'])}</td>
                        <td class="metric">{format_ns(r['p95_ns'])}</td>
                        <td class="metric">{throughput_str}</td>
"""
        
        if comparison_data and r['name'] in comparison_map:
            comp = comparison_map[r['name']]
            diff_pct = comp['diff_pct']
            css_class = 'improved' if diff_pct < -5 else ('regressed' if diff_pct > 5 else '')
            sign = '+' if diff_pct > 0 else ''
            html += f"""
                        <td class="metric {css_class}">{sign}{diff_pct:.2f}%</td>
"""
        elif comparison_data:
            html += """
                        <td class="metric">New</td>
"""
        
        html += """
                    </tr>
"""
    
    html += """
                </tbody>
            </table>
        </div>
"""
    
    # Add JavaScript for charts
    html += """
        <footer class="footer">
            <p>Generated by json.cpp benchmark visualization tool</p>
        </footer>
    </div>
    
    <script>
        // Chart.js default settings
        Chart.defaults.font.family = '-apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif';
        Chart.defaults.color = '""" + text_color + """';
        
        const benchmarkNames = """ + json.dumps(benchmark_names) + """;
        const medianTimes = """ + json.dumps(median_times) + """;
        const meanTimes = """ + json.dumps(mean_times) + """;
        const minTimes = """ + json.dumps(min_times) + """;
        const maxTimes = """ + json.dumps(max_times) + """;
        const p95Times = """ + json.dumps(p95_times) + """;
        const throughputs = """ + json.dumps(throughputs) + """;
"""
    
    if comparison_data:
        comparison_names = [c['name'] for c in comparison_data]
        baseline_times = [c['baseline'] for c in comparison_data]
        current_times = [c['current'] for c in comparison_data]
        
        html += """
        const comparisonNames = """ + json.dumps(comparison_names) + """;
        const baselineTimes = """ + json.dumps(baseline_times) + """;
        const currentTimes = """ + json.dumps(current_times) + """;
        
        // Comparison Chart
        new Chart(document.getElementById('comparisonChart'), {
            type: 'bar',
            data: {
                labels: comparisonNames,
                datasets: [
                    {
                        label: 'Baseline (ns)',
                        data: baselineTimes,
                        backgroundColor: 'rgba(108, 117, 125, 0.7)',
                        borderColor: 'rgba(108, 117, 125, 1)',
                        borderWidth: 1
                    },
                    {
                        label: 'Current (ns)',
                        data: currentTimes,
                        backgroundColor: 'rgba(0, 123, 255, 0.7)',
                        borderColor: 'rgba(0, 123, 255, 1)',
                        borderWidth: 1
                    }
                ]
            },
            options: {
                responsive: true,
                maintainAspectRatio: true,
                plugins: {
                    legend: {
                        position: 'top',
                    },
                    tooltip: {
                        callbacks: {
                            label: function(context) {
                                let label = context.dataset.label || '';
                                if (label) {
                                    label += ': ';
                                }
                                const value = context.parsed.y;
                                if (value >= 1000000) {
                                    label += (value / 1000000).toFixed(2) + ' ms';
                                } else if (value >= 1000) {
                                    label += (value / 1000).toFixed(2) + ' μs';
                                } else {
                                    label += value.toFixed(2) + ' ns';
                                }
                                return label;
                            }
                        }
                    }
                },
                scales: {
                    y: {
                        beginAtZero: true,
                        title: {
                            display: true,
                            text: 'Time (ns)'
                        },
                        grid: {
                            color: '""" + grid_color + """'
                        }
                    },
                    x: {
                        grid: {
                            display: false
                        }
                    }
                }
            }
        });
"""
    
    html += """
        // Median Time Chart
        new Chart(document.getElementById('medianChart'), {
            type: 'bar',
            data: {
                labels: benchmarkNames,
                datasets: [{
                    label: 'Median Time (ns)',
                    data: medianTimes,
                    backgroundColor: 'rgba(0, 123, 255, 0.7)',
                    borderColor: 'rgba(0, 123, 255, 1)',
                    borderWidth: 1
                }]
            },
            options: {
                responsive: true,
                maintainAspectRatio: true,
                plugins: {
                    legend: {
                        display: false
                    },
                    tooltip: {
                        callbacks: {
                            label: function(context) {
                                const value = context.parsed.y;
                                if (value >= 1000000) {
                                    return (value / 1000000).toFixed(2) + ' ms';
                                } else if (value >= 1000) {
                                    return (value / 1000).toFixed(2) + ' μs';
                                } else {
                                    return value.toFixed(2) + ' ns';
                                }
                            }
                        }
                    }
                },
                scales: {
                    y: {
                        beginAtZero: true,
                        title: {
                            display: true,
                            text: 'Time (ns)'
                        },
                        grid: {
                            color: '""" + grid_color + """'
                        }
                    },
                    x: {
                        grid: {
                            display: false
                        }
                    }
                }
            }
        });
        
        // Distribution Chart
        new Chart(document.getElementById('distributionChart'), {
            type: 'line',
            data: {
                labels: benchmarkNames,
                datasets: [
                    {
                        label: 'Min',
                        data: minTimes,
                        borderColor: 'rgba(40, 167, 69, 1)',
                        backgroundColor: 'rgba(40, 167, 69, 0.1)',
                        borderWidth: 2,
                        fill: false
                    },
                    {
                        label: 'Median',
                        data: medianTimes,
                        borderColor: 'rgba(0, 123, 255, 1)',
                        backgroundColor: 'rgba(0, 123, 255, 0.1)',
                        borderWidth: 2,
                        fill: false
                    },
                    {
                        label: 'Mean',
                        data: meanTimes,
                        borderColor: 'rgba(255, 193, 7, 1)',
                        backgroundColor: 'rgba(255, 193, 7, 0.1)',
                        borderWidth: 2,
                        fill: false
                    },
                    {
                        label: 'P95',
                        data: p95Times,
                        borderColor: 'rgba(255, 87, 34, 1)',
                        backgroundColor: 'rgba(255, 87, 34, 0.1)',
                        borderWidth: 2,
                        fill: false
                    },
                    {
                        label: 'Max',
                        data: maxTimes,
                        borderColor: 'rgba(220, 53, 69, 1)',
                        backgroundColor: 'rgba(220, 53, 69, 0.1)',
                        borderWidth: 2,
                        fill: false
                    }
                ]
            },
            options: {
                responsive: true,
                maintainAspectRatio: true,
                plugins: {
                    legend: {
                        position: 'top',
                    },
                    tooltip: {
                        callbacks: {
                            label: function(context) {
                                let label = context.dataset.label || '';
                                if (label) {
                                    label += ': ';
                                }
                                const value = context.parsed.y;
                                if (value >= 1000000) {
                                    label += (value / 1000000).toFixed(2) + ' ms';
                                } else if (value >= 1000) {
                                    label += (value / 1000).toFixed(2) + ' μs';
                                } else {
                                    label += value.toFixed(2) + ' ns';
                                }
                                return label;
                            }
                        }
                    }
                },
                scales: {
                    y: {
                        beginAtZero: true,
                        title: {
                            display: true,
                            text: 'Time (ns)'
                        },
                        grid: {
                            color: '""" + grid_color + """'
                        }
                    },
                    x: {
                        grid: {
                            display: false
                        }
                    }
                }
            }
        });
"""
    
    # Add throughput chart if applicable
    if any(t > 0 for t in throughputs):
        html += """
        // Throughput Chart
        const throughputLabels = benchmarkNames.filter((_, i) => throughputs[i] > 0);
        const throughputData = throughputs.filter(t => t > 0);
        
        new Chart(document.getElementById('throughputChart'), {
            type: 'bar',
            data: {
                labels: throughputLabels,
                datasets: [{
                    label: 'Throughput (MB/s)',
                    data: throughputData,
                    backgroundColor: 'rgba(40, 167, 69, 0.7)',
                    borderColor: 'rgba(40, 167, 69, 1)',
                    borderWidth: 1
                }]
            },
            options: {
                responsive: true,
                maintainAspectRatio: true,
                plugins: {
                    legend: {
                        display: false
                    }
                },
                scales: {
                    y: {
                        beginAtZero: true,
                        title: {
                            display: true,
                            text: 'MB/s'
                        },
                        grid: {
                            color: '""" + grid_color + """'
                        }
                    },
                    x: {
                        grid: {
                            display: false
                        }
                    }
                }
            }
        });
"""
    
    html += """
    </script>
</body>
</html>
"""
    
    return html


def main():
    parser = argparse.ArgumentParser(
        description="Generate HTML visualization report from benchmark results",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__
    )
    parser.add_argument("results", help="Benchmark results JSON file")
    parser.add_argument("--output", "-o", default="benchmark_report.html",
                       help="Output HTML file (default: benchmark_report.html)")
    parser.add_argument("--title", default="json.cpp Benchmark Report",
                       help="Report title (default: json.cpp Benchmark Report)")
    parser.add_argument("--baseline", help="Baseline JSON file for comparison")
    parser.add_argument("--theme", choices=["light", "dark"], default="light",
                       help="Color theme (default: light)")
    
    args = parser.parse_args()
    
    # Load results
    results_data = load_results(args.results)
    
    baseline_data = None
    if args.baseline:
        baseline_data = load_results(args.baseline)
    
    # Generate HTML
    html = generate_html(results_data, baseline_data, args.title, args.theme)
    
    # Write output
    try:
        with open(args.output, 'w') as f:
            f.write(html)
        print(f"HTML report generated: {args.output}", file=sys.stderr)
    except Exception as e:
        print(f"Error writing to {args.output}: {e}", file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    main()
