# Copilot Instructions for json.cpp

## Project Overview

**json.cpp** is a high-performance C++ JSON parsing and serialization library optimized for speed and minimal code footprint. It prioritizes:
- **2-3x faster parsing** than nlohmann/json via aggressive optimizations
- **~1500 lines of code** (vs 24,000+ in nlohmann) for maintainability
- **Full JSONTestSuite compliance** for spec correctness
- **Float32 preservation** using vendored double-conversion library for precision

## Architecture

### Core Components

- **`json.h`** (233 lines): Public API with `Json` class using a tagged union for efficient type storage
- **`json.cpp`** (1,303 lines): Parser state machine + serialization. Uses character classification tables (`kJsonStr`) for O(1) validation
- **`double-conversion/`**: Vendored library for fast float↔string conversion with precision preservation
- **`json_test.cpp`**: Unit tests covering parsing, serialization, nested structures, JSONPath queries, and performance benchmarks
- **`jsontestsuite_test.cpp`**: Full compliance against JSONTestSuite corpus (~500+ test cases)
- **`benchmarks/json_perf.cpp`**: Performance profiling with synthetic corpora

### Key Design Patterns

**Type System**: 8-state enum (`Null`, `Bool`, `Long`, `Float`, `Double`, `String`, `Array`, `Object`) with union-based storage. Floats are explicitly preserved—assigning `3.14f` stores as `Float` not `Double`.

**Parse Error Reporting**: Returns `std::pair<Status, Json>` with ~30 status codes for precise error diagnosis (e.g., `unexpected_octal` vs `bad_double`). See `Status` enum in `json.h`.

**JSONPath Support**: Methods `jsonpath()` / `updateJsonpath()` / `deleteJsonpath()` implement filter expressions like `$.store.book[?(@.price < 10)]`.

## Development Workflows

### Build & Test

```bash
./build.sh                      # Build + run all tests (default)
./build.sh build --no-test      # Build only, skip tests
./build.sh test                 # Run tests only
./build.sh rebuild              # Clean + rebuild
./build.sh perf                 # Run performance benchmarks
```

**Behind the scenes**: CMake generates build/, compiles `json.cpp` → `libjson.a`, then links test executables.

### Key Test Targets

- **`json_test`**: ~10 micro-benchmarks (object creation, parsing, serialization, JSONPath queries)
- **`jsontestsuite_test`**: Validates against 400+ JSON corpus files; reports pass/fail + error type
- **`json_perf`**: Extended benchmarking with large synthetic JSON; reports min/mean/median latency + throughput

**Run individual tests**: `ctest -N` lists all; `ctest -R pattern` filters by name.

## Common Patterns

### Parsing with Error Handling

```cpp
auto [status, json] = Json::parse(payload_string);
if (status != Json::success) {
    std::cerr << "Parse error: " << Json::StatusToString(status) << std::endl;
    return handle_error(status);
}
if (!json.isObject()) return handle_type_error();
```

### Safe Type Navigation

Always check type before access:
```cpp
if (json["field"].isString()) {
    std::string val = json["field"].getString();
}
```

Type getters throw or abort if type mismatch—use `isX()` predicates to validate first.

### Preserving Numeric Precision

```cpp
Json j;
j["f32"] = 3.14f;      // Stored as Float, serialized as float32
j["f64"] = 2.71828;    // Stored as Double
j["int"] = 42LL;       // Stored as Long (int64_t)
// toString() respects original types for correct precision output
```

### JSONPath Queries

```cpp
// Query returns vector of pointers to matching nodes
for (Json* node : json.jsonpath("$.store.book[?(@.price < 10)].title")) {
    process(node->getString());
}

// Mutable and const versions available
const std::vector<const Json*>& results = const_json.jsonpath("$..author");
```

## Performance Considerations

- **Parser state machine** is hand-optimized with LUT-based character classification
- **Double-conversion library** avoids sprintf for ~2x float speedup
- **Move semantics** used throughout; prefer `std::move()` for arrays/objects
- **Depth limit** set to 20 levels (configurable `DEPTH` macro); deeper structures rejected
- **Stack depth** monitored; returns `stack_overflow` if exceeded

For benchmarking, use `json_perf` with corpus files in `benchmarks/corpus/`.

## Code Conventions

- **Namespace**: `jt::Json` (not `json` to avoid conflicts)
- **Indentation**: 4 spaces (enforced by `.clang-format`)
- **Error handling**: Return `Status` enum, throw only for logic errors if `__cpp_exceptions` defined
- **Memory**: Use STL containers (`std::string`, `std::map`, `std::vector`); union-based storage avoids heap bloat

## Critical Files to Reference

| File | Purpose |
|------|---------|
| `json.h` | Public API; read for interface contract |
| `json.cpp:parse()` | Core state machine; reference for error handling paths |
| `example/main.cpp` | 8 usage examples covering all major APIs |
| `json_test.cpp` | Micro-benchmarks + integration tests |
| `CMakeLists.txt` | Build config; controls vendored double-conversion inclusion |

## Integration Notes

- Consumers typically include only `json.h` and link against compiled `libjson.a`
- Double-conversion is vendored; no external dependency needed
- C++11 standard required; compiles with GCC 4.8+ or Clang 3.3+
- Suitable for embedded (low compile overhead) or high-throughput (parsing speed) scenarios

## Testing New Features

1. Add test case to `json_test.cpp` (or new category file)
2. Run `./build.sh test` to validate
3. If affecting parsing, verify against JSONTestSuite: `ctest -R jsontestsuite_test`
4. Check performance regression: `./build.sh perf --runs 5`
