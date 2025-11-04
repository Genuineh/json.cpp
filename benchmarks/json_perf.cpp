#include "json.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <functional>
#include <numeric>
#include <sstream>
#include <string>
#include <vector>

#ifndef JTJSON_SOURCE_DIR
#error "JTJSON_SOURCE_DIR must be defined"
#endif

namespace bench
{

using Clock = std::chrono::high_resolution_clock;

struct Stats
{
    double min_ns;
    double max_ns;
    double mean_ns;
    double median_ns;
    double stddev_ns;
    double p95_ns;
    double p99_ns;
};

struct BenchConfig
{
    std::size_t warmup_runs = 1;
    std::size_t measure_runs = 5;
    double scale = 1.0;
    std::string filter;
    bool list_only = false;
    bool generate_report = false;
    std::string report_format = "text"; // text, csv, json, markdown
};

struct BenchCase
{
    std::string name;
    std::size_t inner_iterations;
    std::size_t bytes_per_iteration;
    std::function<void(std::size_t)> prepare;
    std::function<void()> body;
};

struct BenchResult
{
    std::string name;
    Stats stats;
    std::size_t iterations;
    std::size_t bytes_per_iteration;
    double throughput_mb_s;
};

static volatile std::uint64_t g_sink = 0;

template <class T>
inline void DoNotOptimize(const T& value)
{
#if defined(__GNUC__) || defined(__clang__)
    asm volatile("" : : "g"(value) : "memory");
#else
    std::atomic_signal_fence(std::memory_order_acq_rel);
#endif
}

inline void ClobberMemory()
{
#if defined(__GNUC__) || defined(__clang__)
    asm volatile("" : : : "memory");
#else
    std::atomic_signal_fence(std::memory_order_acq_rel);
#endif
}

inline void Ensure(bool condition, const std::string& message)
{
    if (!condition) {
        std::fprintf(stderr, "error: %s\n", message.c_str());
        std::exit(1);
    }
}

inline Stats
ComputeStats(std::vector<double> samples)
{
    Ensure(!samples.empty(), "ComputeStats called with empty samples");
    Stats stats;
    stats.min_ns = *std::min_element(samples.begin(), samples.end());
    stats.max_ns = *std::max_element(samples.begin(), samples.end());
    const double sum = std::accumulate(samples.begin(), samples.end(), 0.0);
    stats.mean_ns = sum / samples.size();
    double variance = 0.0;
    for (std::size_t i = 0; i < samples.size(); ++i) {
        const double diff = samples[i] - stats.mean_ns;
        variance += diff * diff;
    }
    variance /= samples.size();
    stats.stddev_ns = std::sqrt(variance);
    std::sort(samples.begin(), samples.end());
    if (samples.size() % 2 == 0) {
        std::size_t idx = samples.size() / 2;
        stats.median_ns = (samples[idx - 1] + samples[idx]) * 0.5;
    } else {
        stats.median_ns = samples[samples.size() / 2];
    }
    // Calculate percentiles with proper interpolation for small sample sizes
    // For small samples (< 20), percentiles may equal max value
    if (samples.size() >= 20) {
        std::size_t p95_idx = static_cast<std::size_t>((samples.size() - 1) * 0.95);
        std::size_t p99_idx = static_cast<std::size_t>((samples.size() - 1) * 0.99);
        stats.p95_ns = samples[p95_idx];
        stats.p99_ns = samples[p99_idx];
    } else {
        // For small sample sizes, use max as conservative estimate
        stats.p95_ns = stats.max_ns;
        stats.p99_ns = stats.max_ns;
    }
    return stats;
}

inline std::size_t
ClampIterations(std::size_t base, double scale)
{
    if (base == 0) {
        return 1;
    }
    double scaled = base * scale;
    if (scaled < 1.0) {
        return 1;
    }
    return static_cast<std::size_t>(scaled);
}

inline void
PrintTextReport(const std::vector<BenchResult>& results, const BenchConfig& config)
{
    std::printf("\n=== Performance Benchmark Report ===\n");
    std::printf("Configuration: warmup=%zu runs=%zu scale=%.2f\n\n",
                config.warmup_runs, config.measure_runs, config.scale);
    
    for (const auto& r : results) {
        std::printf("%-32s %10.2f ns/op  (median %.2f | min %.2f | max %.2f | stddev %.2f | p95 %.2f | p99 %.2f)  iter=%-6zu",
                    r.name.c_str(),
                    r.stats.mean_ns,
                    r.stats.median_ns,
                    r.stats.min_ns,
                    r.stats.max_ns,
                    r.stats.stddev_ns,
                    r.stats.p95_ns,
                    r.stats.p99_ns,
                    r.iterations);
        if (r.throughput_mb_s > 0.0) {
            std::printf("  throughput=%.2f MB/s", r.throughput_mb_s);
        }
        std::printf("\n");
    }
}

inline void
PrintCSVReport(const std::vector<BenchResult>& results)
{
    std::printf("benchmark,mean_ns,median_ns,min_ns,max_ns,stddev_ns,p95_ns,p99_ns,iterations,bytes_per_iter,throughput_mb_s\n");
    for (const auto& r : results) {
        // Sanitize benchmark name by replacing commas with semicolons to avoid CSV issues
        std::string safe_name = r.name;
        for (std::size_t i = 0; i < safe_name.size(); ++i) {
            if (safe_name[i] == ',') safe_name[i] = ';';
        }
        std::printf("%s,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%zu,%zu,%.2f\n",
                    safe_name.c_str(),
                    r.stats.mean_ns,
                    r.stats.median_ns,
                    r.stats.min_ns,
                    r.stats.max_ns,
                    r.stats.stddev_ns,
                    r.stats.p95_ns,
                    r.stats.p99_ns,
                    r.iterations,
                    r.bytes_per_iteration,
                    r.throughput_mb_s);
    }
}

inline void
PrintJSONReport(const std::vector<BenchResult>& results, const BenchConfig& config)
{
    std::printf("{\n");
    std::printf("  \"config\": {\n");
    std::printf("    \"warmup_runs\": %zu,\n", config.warmup_runs);
    std::printf("    \"measure_runs\": %zu,\n", config.measure_runs);
    std::printf("    \"scale\": %.2f\n", config.scale);
    std::printf("  },\n");
    std::printf("  \"results\": [\n");
    for (std::size_t i = 0; i < results.size(); ++i) {
        const auto& r = results[i];
        std::printf("    {\n");
        std::printf("      \"name\": \"%s\",\n", r.name.c_str());
        std::printf("      \"mean_ns\": %.2f,\n", r.stats.mean_ns);
        std::printf("      \"median_ns\": %.2f,\n", r.stats.median_ns);
        std::printf("      \"min_ns\": %.2f,\n", r.stats.min_ns);
        std::printf("      \"max_ns\": %.2f,\n", r.stats.max_ns);
        std::printf("      \"stddev_ns\": %.2f,\n", r.stats.stddev_ns);
        std::printf("      \"p95_ns\": %.2f,\n", r.stats.p95_ns);
        std::printf("      \"p99_ns\": %.2f,\n", r.stats.p99_ns);
        std::printf("      \"iterations\": %zu,\n", r.iterations);
        std::printf("      \"bytes_per_iteration\": %zu,\n", r.bytes_per_iteration);
        std::printf("      \"throughput_mb_s\": %.2f\n", r.throughput_mb_s);
        std::printf("    }%s\n", (i < results.size() - 1) ? "," : "");
    }
    std::printf("  ]\n");
    std::printf("}\n");
}

inline void
PrintMarkdownReport(const std::vector<BenchResult>& results, const BenchConfig& config)
{
    std::printf("# Performance Benchmark Report\n\n");
    std::printf("## Configuration\n\n");
    std::printf("- Warmup runs: %zu\n", config.warmup_runs);
    std::printf("- Measurement runs: %zu\n", config.measure_runs);
    std::printf("- Scale factor: %.2f\n\n", config.scale);
    
    std::printf("## Results\n\n");
    std::printf("| Benchmark | Mean (ns) | Median (ns) | Min (ns) | Max (ns) | StdDev (ns) | P95 (ns) | P99 (ns) | Throughput (MB/s) |\n");
    std::printf("|-----------|-----------|-------------|----------|----------|-------------|----------|----------|-------------------|\n");
    
    for (const auto& r : results) {
        std::printf("| %s | %.2f | %.2f | %.2f | %.2f | %.2f | %.2f | %.2f | ",
                    r.name.c_str(),
                    r.stats.mean_ns,
                    r.stats.median_ns,
                    r.stats.min_ns,
                    r.stats.max_ns,
                    r.stats.stddev_ns,
                    r.stats.p95_ns,
                    r.stats.p99_ns);
        if (r.throughput_mb_s > 0.0) {
            std::printf("%.2f", r.throughput_mb_s);
        } else {
            std::printf("N/A");
        }
        std::printf(" |\n");
    }
    std::printf("\n");
}


class Runner
{
  public:
    explicit Runner(const BenchConfig& cfg) : config_(cfg)
    {
    }

    void run(const BenchCase& bench_case)
    {
        if (!config_.filter.empty() &&
            bench_case.name.find(config_.filter) == std::string::npos) {
            return;
        }

        const std::size_t inner = ClampIterations(bench_case.inner_iterations, config_.scale);
        Ensure(inner > 0, "inner iterations must be positive");

        if (config_.list_only) {
            std::printf("%s\n", bench_case.name.c_str());
            return;
        }

        // Warmup runs
        for (std::size_t w = 0; w < config_.warmup_runs; ++w) {
            if (bench_case.prepare) {
                bench_case.prepare(inner);
            }
            for (std::size_t i = 0; i < inner; ++i) {
                bench_case.body();
            }
        }

        std::vector<double> samples;
        samples.reserve(config_.measure_runs);

        for (std::size_t run = 0; run < config_.measure_runs; ++run) {
            if (bench_case.prepare) {
                bench_case.prepare(inner);
            }
            Clock::time_point start = Clock::now();
            for (std::size_t i = 0; i < inner; ++i) {
                bench_case.body();
            }
            Clock::time_point end = Clock::now();
            double total_ns = static_cast<double>(
              std::chrono::duration_cast<std::chrono::nanoseconds>(end - start)
                .count());
            samples.push_back(total_ns / inner);
            ClobberMemory();
        }

        Stats stats = ComputeStats(samples);

        double throughput_mb_s = 0.0;
        if (bench_case.bytes_per_iteration > 0 && stats.median_ns > 0.0) {
            throughput_mb_s = (bench_case.bytes_per_iteration * 1e3) / stats.median_ns;
        }

        BenchResult result;
        result.name = bench_case.name;
        result.stats = stats;
        result.iterations = inner;
        result.bytes_per_iteration = bench_case.bytes_per_iteration;
        result.throughput_mb_s = throughput_mb_s;
        results_.push_back(result);

        if (!config_.generate_report) {
            std::printf("%-32s %10.2f ns/op  (median %.2f | min %.2f | max %.2f | stddev %.2f)  inner=%-6zu",
                        bench_case.name.c_str(),
                        stats.mean_ns,
                        stats.median_ns,
                        stats.min_ns,
                        stats.max_ns,
                        stats.stddev_ns,
                        inner);
            if (throughput_mb_s > 0.0) {
                std::printf("  throughput=%.2f MB/s",
                            throughput_mb_s);
            }
            std::printf("\n");
        }
    }

    const std::vector<BenchResult>& getResults() const
    {
        return results_;
    }

  private:
    BenchConfig config_;
    std::vector<BenchResult> results_;
};

inline std::string
ReadTextFile(const std::string& path)
{
    std::ifstream file(path.c_str(), std::ios::in | std::ios::binary);
    Ensure(file.good(), std::string("unable to open file: ") + path);
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

struct Corpus
{
    std::string name;
    std::vector<std::string> files;
    std::size_t total_bytes;
};

inline bool
HasPrefix(const std::string& s, const std::string& prefix)
{
    return s.compare(0, prefix.size(), prefix) == 0;
}

inline Corpus
LoadCorpus(const std::string& dir, const std::string& prefix, std::size_t limit)
{
    Corpus corpus;
    corpus.total_bytes = 0;
    DIR* handle = opendir(dir.c_str());
    Ensure(handle != NULL, std::string("unable to open directory: ") + dir);
    std::vector<std::string> paths;
    struct dirent* entry;
    while ((entry = readdir(handle)) != NULL) {
        const char* name = entry->d_name;
        if (name[0] == '.') {
            continue;
        }
        std::string filename(name);
        if (!prefix.empty() && !HasPrefix(filename, prefix)) {
            continue;
        }
        std::string path = dir + "/" + filename;
        paths.push_back(path);
    }
    closedir(handle);

    std::sort(paths.begin(), paths.end());
    for (std::size_t i = 0; i < paths.size(); ++i) {
        if (limit > 0 && corpus.files.size() >= limit) {
            break;
        }
        const std::string& path = paths[i];
        std::string contents = ReadTextFile(path);
        corpus.total_bytes += contents.size();
        corpus.files.push_back(contents);
    }
    corpus.name = dir;
    return corpus;
}

inline BenchConfig
ParseArgs(int argc, char** argv)
{
    BenchConfig config;
    for (int i = 1; i < argc; ++i) {
        std::string arg(argv[i]);
        if (arg == "--help" || arg == "-h") {
            std::printf("json_perf options:\n");
            std::printf("  --warmup N       Number of warmup runs (default 1)\n");
            std::printf("  --runs N         Number of measured runs (default 5)\n");
            std::printf("  --scale X        Scale inner iteration counts by X\n");
            std::printf("  --filter STR     Only run benchmarks containing STR\n");
            std::printf("  --list           List benchmark names\n");
            std::printf("  --report FORMAT  Generate report (text, csv, json, markdown)\n");
            std::exit(0);
        } else if (HasPrefix(arg, "--warmup=")) {
            config.warmup_runs = static_cast<std::size_t>(std::strtoul(arg.c_str() + 9, NULL, 10));
        } else if (HasPrefix(arg, "--runs=")) {
            config.measure_runs = static_cast<std::size_t>(std::strtoul(arg.c_str() + 7, NULL, 10));
        } else if (HasPrefix(arg, "--scale=")) {
            config.scale = std::atof(arg.c_str() + 8);
        } else if (HasPrefix(arg, "--filter=")) {
            config.filter = arg.substr(9);
        } else if (HasPrefix(arg, "--report=")) {
            config.generate_report = true;
            config.report_format = arg.substr(9);
        } else if (arg == "--warmup") {
            Ensure(i + 1 < argc, "--warmup requires an argument");
            config.warmup_runs = static_cast<std::size_t>(std::strtoul(argv[++i], NULL, 10));
        } else if (arg == "--runs") {
            Ensure(i + 1 < argc, "--runs requires an argument");
            config.measure_runs = static_cast<std::size_t>(std::strtoul(argv[++i], NULL, 10));
        } else if (arg == "--scale") {
            Ensure(i + 1 < argc, "--scale requires an argument");
            config.scale = std::atof(argv[++i]);
        } else if (arg == "--filter") {
            Ensure(i + 1 < argc, "--filter requires an argument");
            config.filter = argv[++i];
        } else if (arg == "--list") {
            config.list_only = true;
        } else if (arg == "--report") {
            Ensure(i + 1 < argc, "--report requires an argument");
            config.generate_report = true;
            config.report_format = argv[++i];
        } else {
            Ensure(false, std::string("unknown argument: ") + arg);
        }
    }
    if (config.measure_runs == 0) {
        config.measure_runs = 1;
    }
    return config;
}

} // namespace bench

static const char kStoreExample[] =
  R"({
  "store": {
    "book": [
      {
        "category": "reference",
        "author": "Nigel Rees",
        "title": "Sayings of the Century",
        "price": 8.95
      },
      {
        "category": "fiction",
        "author": "Evelyn Waugh",
        "title": "Sword of Honour",
        "price": 12.99
      },
      {
        "category": "fiction",
        "author": "Herman Melville",
        "title": "Moby Dick",
        "isbn": "0-553-21311-3",
        "price": 8.99
      },
      {
        "category": "fiction",
        "author": "J. R. R. Tolkien",
        "title": "The Lord of the Rings",
        "isbn": "0-395-19395-8",
        "price": 22.99
      }
    ],
    "bicycle": {
      "color": "red",
      "price": 19.95
    }
  },
  "expensive": 10
})";

static const char kLargeJsonExample[] =
  R"({
  "store": {
    "book": [
      {"category": "reference", "author": "Nigel Rees", "title": "Sayings of the Century", "price": 8.95},
      {"category": "fiction", "author": "Evelyn Waugh", "title": "Sword of Honour", "price": 12.99},
      {"category": "fiction", "author": "Herman Melville", "title": "Moby Dick", "isbn": "0-553-21311-3", "price": 8.99},
      {"category": "fiction", "author": "J. R. R. Tolkien", "title": "The Lord of the Rings", "isbn": "0-395-19395-8", "price": 22.99},
      {"category": "fiction", "author": "Jane Austen", "title": "Pride and Prejudice", "price": 9.95},
      {"category": "fiction", "author": "Charles Dickens", "title": "A Tale of Two Cities", "price": 11.50},
      {"category": "reference", "author": "John Doe", "title": "Technical Manual", "price": 15.00},
      {"category": "fiction", "author": "Mark Twain", "title": "Adventures of Huckleberry Finn", "price": 7.99}
    ],
    "bicycle": {"color": "red", "price": 19.95},
    "car": {"color": "blue", "price": 29999.99},
    "electronics": [
      {"name": "laptop", "price": 1299.99, "stock": 10},
      {"name": "phone", "price": 899.99, "stock": 25},
      {"name": "tablet", "price": 599.99, "stock": 15}
    ]
  },
  "expensive": 10
})";

int
main(int argc, char** argv)
{
    using namespace bench;

    BenchConfig config = ParseArgs(argc, argv);

    const std::string medium_orders_path =
      std::string(JTJSON_SOURCE_DIR) + "/benchmarks/corpus/medium_orders.json";
    const std::string large_orders_path =
      std::string(JTJSON_SOURCE_DIR) + "/benchmarks/corpus/large_orders.json";
    const std::string invalid_deep_path =
      std::string(JTJSON_SOURCE_DIR) +
      "/JSONTestSuite/test_parsing/n_structure_100000_opening_arrays.json";
    const std::string suite_dir =
      std::string(JTJSON_SOURCE_DIR) + "/JSONTestSuite/test_parsing";

    const std::string medium_orders = ReadTextFile(medium_orders_path);
    const std::string large_orders = ReadTextFile(large_orders_path);
    const std::string invalid_deep = ReadTextFile(invalid_deep_path);

    const bench::Corpus valid_corpus = LoadCorpus(suite_dir, "y_", 0);
    const bench::Corpus invalid_corpus = LoadCorpus(suite_dir, "n_", 0);

    jt::Json medium_orders_json = jt::Json::parse(medium_orders).second;
    jt::Json large_orders_json = jt::Json::parse(large_orders).second;
    std::pair<jt::Json::Status, jt::Json> jsonpath_parsed =
      jt::Json::parse(kLargeJsonExample);
    Ensure(jsonpath_parsed.first == jt::Json::success,
           "failed to parse large json example");
    jt::Json jsonpath_fixture = jsonpath_parsed.second;

    const std::size_t store_literal_bytes = sizeof(kStoreExample) - 1;
    const std::size_t medium_orders_bytes = medium_orders.size();
    const std::size_t large_orders_bytes = large_orders.size();
    const std::size_t invalid_deep_bytes = invalid_deep.size();
    const std::size_t medium_compact_bytes = medium_orders_json.toString().size();
    const std::size_t medium_pretty_bytes = medium_orders_json.toStringPretty().size();
    const std::size_t large_compact_bytes = large_orders_json.toString().size();

    std::vector<BenchCase> cases;

    cases.push_back({ "parse.small_literal",
                      4000,
                      store_literal_bytes,
                      std::function<void(std::size_t)>(),
                      [&]() {
                          std::pair<jt::Json::Status, jt::Json> parsed =
                            jt::Json::parse(kStoreExample);
                          Ensure(parsed.first == jt::Json::success,
                                 "parse.small_literal failed");
                          g_sink += parsed.second.isObject();
                      } });

    cases.push_back({ "parse.medium_orders",
                      20,
                      medium_orders_bytes,
                      std::function<void(std::size_t)>(),
                      [&]() {
                          std::pair<jt::Json::Status, jt::Json> parsed =
                            jt::Json::parse(medium_orders);
                          Ensure(parsed.first == jt::Json::success,
                                 "parse.medium_orders failed");
                          g_sink += parsed.second.isArray();
                      } });

    cases.push_back({ "parse.large_orders",
                      4,
                      large_orders_bytes,
                      std::function<void(std::size_t)>(),
                      [&]() {
                          std::pair<jt::Json::Status, jt::Json> parsed =
                            jt::Json::parse(large_orders);
                          Ensure(parsed.first == jt::Json::success,
                                 "parse.large_orders failed");
                          g_sink += parsed.second.isArray();
                      } });

    cases.push_back({ "parse.corpus_valid",
                      1,
                      valid_corpus.total_bytes,
                      std::function<void(std::size_t)>(),
                      [&]() {
                          for (std::size_t i = 0; i < valid_corpus.files.size(); ++i) {
                              const std::string& doc = valid_corpus.files[i];
                              std::pair<jt::Json::Status, jt::Json> parsed =
                                jt::Json::parse(doc);
                              if (parsed.first != jt::Json::success) {
                                  std::fprintf(stderr,
                                               "parse.corpus_valid failed on document %zu\n",
                                               static_cast<std::size_t>(i));
                                  std::exit(1);
                              }
                              g_sink += parsed.second.isObject();
                          }
                      } });

    cases.push_back({ "parse.corpus_invalid",
                      1,
                      invalid_corpus.total_bytes,
                      std::function<void(std::size_t)>(),
                      [&]() {
                          for (std::size_t i = 0; i < invalid_corpus.files.size(); ++i) {
                              const std::string& doc = invalid_corpus.files[i];
                              std::pair<jt::Json::Status, jt::Json> parsed =
                                jt::Json::parse(doc);
                              if (parsed.first == jt::Json::success) {
                                  std::fprintf(stderr,
                                               "parse.corpus_invalid unexpectedly succeeded on %zu\n",
                                               static_cast<std::size_t>(i));
                                  std::exit(1);
                              }
                              g_sink += parsed.first;
                          }
                      } });

    cases.push_back({ "parse.invalid_deep_array",
                      1,
                      invalid_deep_bytes,
                      std::function<void(std::size_t)>(),
                      [&]() {
                          std::pair<jt::Json::Status, jt::Json> parsed =
                            jt::Json::parse(invalid_deep);
                          Ensure(parsed.first != jt::Json::success,
                                 "parse.invalid_deep_array unexpectedly succeeded");
                          g_sink += parsed.first;
                      } });

    cases.push_back({ "stringify.small_compact",
                      4000,
                      medium_compact_bytes,
                      std::function<void(std::size_t)>(),
                      [&]() {
                          std::string out = medium_orders_json.toString();
                          DoNotOptimize(out);
                          g_sink += out.size();
                      } });

    cases.push_back({ "stringify.small_pretty",
                      1000,
                      medium_pretty_bytes,
                      std::function<void(std::size_t)>(),
                      [&]() {
                          std::string out = medium_orders_json.toStringPretty();
                          DoNotOptimize(out);
                          g_sink += out.size();
                      } });

    cases.push_back({ "stringify.large_compact",
                      2,
                      large_compact_bytes,
                      std::function<void(std::size_t)>(),
                      [&]() {
                          std::string out = large_orders_json.toString();
                          DoNotOptimize(out);
                          g_sink += out.size();
                      } });

    cases.push_back({ "jsonpath.query_authors",
                      4000,
                      0,
                      std::function<void(std::size_t)>(),
                      [&]() {
                          std::vector<jt::Json*> authors =
                            jsonpath_fixture.jsonpath("$.store.book[*].author");
                          Ensure(authors.size() == 8,
                                 "jsonpath.query_authors unexpected result size");
                          g_sink += authors.size();
                      } });

    cases.push_back({ "jsonpath.filter_prices",
                      2000,
                      0,
                      std::function<void(std::size_t)>(),
                      [&]() {
                          std::vector<jt::Json*> cheap = jsonpath_fixture.jsonpath(
                            "$.store.book[?(@.price < 10)].title");
                          Ensure(cheap.size() == 4,
                                 "jsonpath.filter_prices unexpected result size");
                          g_sink += cheap.size();
                      } });

    cases.push_back({ "jsonpath.update_prices",
                      200,
                      0,
                      std::function<void(std::size_t)>(),
                      [&]() {
                          jt::Json working = jsonpath_fixture;
                          std::size_t updated = working.updateJsonpath(
                            "$.store.book[*].price",
                            jt::Json(9.99));
                          Ensure(updated == 8,
                                 "jsonpath.update_prices unexpected update count");
                          g_sink += updated;
                      } });

    cases.push_back({ "jsonpath.delete_isbn",
                      200,
                      0,
                      std::function<void(std::size_t)>(),
                      [&]() {
                          jt::Json working = jsonpath_fixture;
                          std::size_t removed = working.deleteJsonpath("$.store.book[*].isbn");
                          Ensure(removed == 2,
                                 "jsonpath.delete_isbn unexpected delete count");
                          g_sink += removed;
                      } });

    cases.push_back({ "roundtrip.medium_orders",
                      4,
                      medium_orders_bytes,
                      std::function<void(std::size_t)>(),
                      [&]() {
                          std::pair<jt::Json::Status, jt::Json> parsed =
                            jt::Json::parse(medium_orders);
                          Ensure(parsed.first == jt::Json::success,
                                 "roundtrip.medium_orders parse failed");
                          std::string out = parsed.second.toString();
                          DoNotOptimize(out);
                          g_sink += out.size();
                      } });

    // Additional comprehensive benchmarks
    cases.push_back({ "construct.empty_object",
                      10000,
                      0,
                      std::function<void(std::size_t)>(),
                      [&]() {
                          jt::Json obj;
                          obj["key"] = "value";
                          DoNotOptimize(obj);
                          g_sink += obj.isObject();
                      } });

    cases.push_back({ "construct.nested_object",
                      5000,
                      0,
                      std::function<void(std::size_t)>(),
                      [&]() {
                          jt::Json obj;
                          obj["a"]["b"]["c"]["d"] = 42;
                          DoNotOptimize(obj);
                          g_sink += obj.isObject();
                      } });

    cases.push_back({ "construct.array_integers",
                      3000,
                      0,
                      std::function<void(std::size_t)>(),
                      [&]() {
                          jt::Json arr;
                          for (int i = 0; i < 10; ++i) {
                              arr[i] = i * 100;
                          }
                          DoNotOptimize(arr);
                          g_sink += arr.isArray();
                      } });

    cases.push_back({ "access.deep_nested",
                      5000,
                      0,
                      std::function<void(std::size_t)>(),
                      [&]() {
                          std::pair<jt::Json::Status, jt::Json> parsed =
                            jt::Json::parse(R"({"a":{"b":{"c":{"d":"value"}}}})");
                          Ensure(parsed.first == jt::Json::success,
                                 "access.deep_nested parse failed");
                          const jt::Json& val = parsed.second["a"]["b"]["c"]["d"];
                          DoNotOptimize(val);
                          g_sink += val.isString();
                      } });

    cases.push_back({ "access.array_iteration",
                      2000,
                      0,
                      std::function<void(std::size_t)>(),
                      [&]() {
                          std::vector<jt::Json>& arr = medium_orders_json.getArray();
                          std::size_t count = 0;
                          for (const auto& item : arr) {
                              count += item.isObject();
                          }
                          g_sink += count;
                      } });

    cases.push_back({ "parse.deeply_nested",
                      100,
                      0,
                      std::function<void(std::size_t)>(),
                      [&]() {
                          std::string deep = "{";
                          for (int i = 0; i < 15; ++i) {
                              deep += "\"a\":{";
                          }
                          deep += "\"value\":42";
                          for (int i = 0; i < 15; ++i) {
                              deep += "}";
                          }
                          deep += "}";
                          std::pair<jt::Json::Status, jt::Json> parsed =
                            jt::Json::parse(deep);
                          Ensure(parsed.first == jt::Json::success,
                                 "parse.deeply_nested failed");
                          g_sink += parsed.second.isObject();
                      } });

    cases.push_back({ "parse.number_array",
                      1000,
                      0,
                      std::function<void(std::size_t)>(),
                      [&]() {
                          std::string numbers = "[";
                          for (int i = 0; i < 100; ++i) {
                              if (i > 0) numbers += ",";
                              numbers += std::to_string(i * 3.14159);
                          }
                          numbers += "]";
                          std::pair<jt::Json::Status, jt::Json> parsed =
                            jt::Json::parse(numbers);
                          Ensure(parsed.first == jt::Json::success,
                                 "parse.number_array failed");
                          g_sink += parsed.second.isArray();
                      } });

    cases.push_back({ "parse.string_array",
                      800,
                      0,
                      std::function<void(std::size_t)>(),
                      [&]() {
                          std::string strings = "[";
                          for (int i = 0; i < 50; ++i) {
                              if (i > 0) strings += ",";
                              strings += "\"string_value_" + std::to_string(i) + "\"";
                          }
                          strings += "]";
                          std::pair<jt::Json::Status, jt::Json> parsed =
                            jt::Json::parse(strings);
                          Ensure(parsed.first == jt::Json::success,
                                 "parse.string_array failed");
                          g_sink += parsed.second.isArray();
                      } });

    cases.push_back({ "stringify.escape_heavy",
                      1000,
                      0,
                      std::function<void(std::size_t)>(),
                      [&]() {
                          jt::Json obj;
                          obj["text"] = "Line 1\nLine 2\tTabbed\r\nQuote: \"Hello\"\\Path";
                          std::string out = obj.toString();
                          DoNotOptimize(out);
                          g_sink += out.size();
                      } });

    cases.push_back({ "copy.medium_object",
                      500,
                      0,
                      std::function<void(std::size_t)>(),
                      [&]() {
                          jt::Json copied = medium_orders_json;
                          DoNotOptimize(copied);
                          g_sink += copied.isArray();
                      } });


    if (config.list_only) {
        for (std::size_t i = 0; i < cases.size(); ++i) {
            Runner(config).run(cases[i]);
        }
        return 0;
    }

    if (!config.generate_report) {
        std::printf("json_perf: warmup=%zu runs=%zu scale=%.2f\n",
                    config.warmup_runs,
                    config.measure_runs,
                    config.scale);
    }

    Runner runner(config);
    for (std::size_t i = 0; i < cases.size(); ++i) {
        runner.run(cases[i]);
    }

    if (config.generate_report) {
        const std::vector<BenchResult>& results = runner.getResults();
        if (config.report_format == "csv") {
            PrintCSVReport(results);
        } else if (config.report_format == "json") {
            PrintJSONReport(results, config);
        } else if (config.report_format == "markdown" || config.report_format == "md") {
            PrintMarkdownReport(results, config);
        } else {
            PrintTextReport(results, config);
        }
    } else {
        std::printf("sink=%llu\n", static_cast<unsigned long long>(g_sink));
    }

    return 0;
}

