#!/bin/bash

# JSON Library Build and Test Script
# This script provides commands to build, test, and clean the project

set -e  # Exit on error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Configuration
BUILD_DIR="build"
BUILD_TYPE="${BUILD_TYPE:-Release}"
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Functions
print_usage() {
    cat << EOF
Usage: $0 [COMMAND] [OPTIONS]

Commands:
    build       Build the project and run tests (default)
    test        Run tests only
    clean       Clean build artifacts
    rebuild     Clean and build
    all         Build and run tests (same as build)
    example     Build example program
    help        Show this help message

Options:
    --type TYPE     Build type: Debug or Release (default: Release)
    --verbose       Enable verbose output
    --no-test       Skip tests when building (only build, don't test)

Examples:
    $0                          # Build the project and run tests
    $0 build                    # Build the project and run tests
    $0 build --no-test          # Build only, skip tests
    $0 test                     # Run tests only
    $0 build --type Debug       # Build in Debug mode and run tests
    $0 all                      # Build and run tests
    $0 clean                    # Clean build artifacts
    $0 rebuild                  # Clean and rebuild

EOF
}

print_info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

print_warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Check if CMake is installed
check_cmake() {
    if ! command -v cmake &> /dev/null; then
        print_error "CMake is not installed. Please install CMake to continue."
        exit 1
    fi
}

# Check if required tools are installed
check_dependencies() {
    if ! command -v g++ &> /dev/null && ! command -v clang++ &> /dev/null; then
        print_error "No C++ compiler found. Please install g++ or clang++."
        exit 1
    fi
    check_cmake
}

# Build the project
build_project() {
    print_info "Building project in ${BUILD_TYPE} mode..."
    
    check_dependencies
    
    # Create build directory if it doesn't exist
    if [ ! -d "$BUILD_DIR" ]; then
        print_info "Creating build directory: $BUILD_DIR"
        mkdir -p "$BUILD_DIR"
    fi
    
    cd "$BUILD_DIR"
    
    # Configure CMake
    print_info "Configuring CMake..."
    cmake .. \
        -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
        -DJSON_CPP_BUILD_TESTS=ON \
        -DDOUBLE_CONVERSION_VENDORED=ON \
        ${VERBOSE_CMAKE}
    
    # Build
    print_info "Building..."
    cmake --build . --config "${BUILD_TYPE}" ${VERBOSE_BUILD}
    
    cd ..
    print_info "Build completed successfully!"
}

# Run tests
run_tests() {
    print_info "Running tests..."
    
    if [ ! -d "$BUILD_DIR" ]; then
        print_warn "Build directory not found. Building first..."
        build_project
    fi
    
    cd "$BUILD_DIR"
    
    # Run CMake tests
    print_info "Running CMake test suite..."
    if ctest --output-on-failure ${VERBOSE_CTEST}; then
        print_info "All tests passed!"
        cd ..
        return 0
    else
        print_error "Some tests failed!"
        cd ..
        return 1
    fi
}

# Clean build artifacts
clean_build() {
    print_info "Cleaning build artifacts..."
    
    if [ -d "$BUILD_DIR" ]; then
        rm -rf "$BUILD_DIR"
        print_info "Removed build directory: $BUILD_DIR"
    fi
    
    # Also clean any .o, .a, .ok files from root (from Makefile)
    if [ -f "Makefile" ]; then
        print_info "Cleaning Makefile artifacts..."
        make clean 2>/dev/null || true
    fi
    
    # Clean example build directory
    if [ -d "example/build" ]; then
        print_info "Cleaning example build directory..."
        rm -rf example/build
    fi
    
    print_info "Clean completed!"
}

# Build example program
build_example() {
    print_info "Building example program..."
    
    check_dependencies
    
    cd example
    
    if [ ! -d "build" ]; then
        mkdir -p build
    fi
    
    cd build
    
    print_info "Configuring CMake for example..."
    cmake .. -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" ${VERBOSE_CMAKE}
    
    print_info "Building example..."
    cmake --build . --config "${BUILD_TYPE}" ${VERBOSE_BUILD}
    
    print_info "Example build completed!"
    print_info "Run with: ./example/build/json_example"
    
    cd ../..
}

# Parse command line arguments
COMMAND="build"
VERBOSE_CMAKE=""
VERBOSE_BUILD=""
VERBOSE_CTEST=""
RUN_TESTS=true

while [[ $# -gt 0 ]]; do
    case $1 in
        build|test|clean|rebuild|all|example|help)
            COMMAND="$1"
            shift
            ;;
        --type)
            BUILD_TYPE="$2"
            if [ "$BUILD_TYPE" != "Debug" ] && [ "$BUILD_TYPE" != "Release" ]; then
                print_error "Invalid build type: $BUILD_TYPE. Use Debug or Release."
                exit 1
            fi
            shift 2
            ;;
        --verbose)
            VERBOSE_CMAKE="-DCMAKE_VERBOSE_MAKEFILE=ON"
            VERBOSE_BUILD="--verbose"
            VERBOSE_CTEST="--verbose"
            shift
            ;;
        --no-test)
            RUN_TESTS=false
            shift
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

# Change to project root directory
# This ensures all relative paths in functions work correctly regardless of
# where the script is invoked from
cd "$PROJECT_ROOT"

# Execute command
case $COMMAND in
    build)
        build_project
        if [ "$RUN_TESTS" = true ]; then
            echo ""
            if run_tests; then
                exit 0
            else
                exit 1
            fi
        fi
        ;;
    test)
        if run_tests; then
            exit 0
        else
            exit 1
        fi
        ;;
    clean)
        clean_build
        ;;
    rebuild)
        clean_build
        echo ""
        build_project
        if [ "$RUN_TESTS" = true ]; then
            echo ""
            if run_tests; then
                exit 0
            else
                exit 1
            fi
        fi
        ;;
    all)
        build_project
        echo ""
        if run_tests; then
            exit 0
        else
            exit 1
        fi
        ;;
    example)
        build_example
        ;;
    help)
        print_usage
        ;;
    *)
        print_error "Unknown command: $COMMAND"
        print_usage
        exit 1
        ;;
esac
