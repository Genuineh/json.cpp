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
};

struct BenchConfig
{
    std::size_t warmup_runs = 1;
    std::size_t measure_runs = 5;
    double scale = 1.0;
    std::string filter;
    bool list_only = false;
};

struct BenchCase
{
    std::string name;
    std::size_t inner_iterations;
    std::size_t bytes_per_iteration;
    std::function<void(std::size_t)> prepare;
    std::function<void()> body;
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

class Runner
{
  public:
    explicit Runner(const BenchConfig& cfg) : config_(cfg)
    {
    }

    void run(const BenchCase& bench_case) const
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

  private:
    BenchConfig config_;
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
            std::printf("  --warmup N     Number of warmup runs (default 1)\n");
            std::printf("  --runs N       Number of measured runs (default 5)\n");
            std::printf("  --scale X      Scale inner iteration counts by X\n");
            std::printf("  --filter STR   Only run benchmarks containing STR\n");
            std::printf("  --list         List benchmark names\n");
            std::exit(0);
        } else if (HasPrefix(arg, "--warmup=")) {
            config.warmup_runs = static_cast<std::size_t>(std::strtoul(arg.c_str() + 9, NULL, 10));
        } else if (HasPrefix(arg, "--runs=")) {
            config.measure_runs = static_cast<std::size_t>(std::strtoul(arg.c_str() + 7, NULL, 10));
        } else if (HasPrefix(arg, "--scale=")) {
            config.scale = std::atof(arg.c_str() + 8);
        } else if (HasPrefix(arg, "--filter=")) {
            config.filter = arg.substr(9);
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

    if (config.list_only) {
        for (std::size_t i = 0; i < cases.size(); ++i) {
            Runner(config).run(cases[i]);
        }
        return 0;
    }

    std::printf("json_perf: warmup=%zu runs=%zu scale=%.2f\n",
                config.warmup_runs,
                config.measure_runs,
                config.scale);

    Runner runner(config);
    for (std::size_t i = 0; i < cases.size(); ++i) {
        runner.run(cases[i]);
    }

    std::printf("sink=%llu\n", static_cast<unsigned long long>(g_sink));

    return 0;
}

