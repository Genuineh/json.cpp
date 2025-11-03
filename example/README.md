# JSON Library Example

This directory contains example code demonstrating basic usage of the `jt::Json` library.

## Building

### Using Make

```bash
cd example
make
./json_example
```

To clean build artifacts:
```bash
make clean
```

### Using CMake

```bash
cd example
mkdir build
cd build
cmake ..
make
./json_example
```

To clean:
```bash
cd build
make clean
# or remove the build directory
cd ..
rm -rf build
```

## What the Example Shows

The example program (`main.cpp`) demonstrates:

1. **Parsing JSON strings** - How to parse JSON from strings and handle errors
2. **Creating JSON objects** - Creating objects programmatically
3. **Creating JSON arrays** - Working with arrays
4. **Accessing and modifying values** - Reading and updating JSON data
5. **Type checking** - Verifying JSON value types before access
6. **Error handling** - Proper error handling for invalid JSON
7. **Nested structures** - Working with complex nested JSON
8. **Serialization** - Converting JSON objects to strings (compact and pretty)

## Dependencies

This example requires:
- C++11 compatible compiler (g++, clang++, etc.)
- The JSON library source files (`../json.h`, `../json.cpp`)
- The double-conversion library (included in `../double-conversion/`)

## File Structure

- `main.cpp` - The example program source code
- `Makefile` - Build script for Make
- `CMakeLists.txt` - Build script for CMake
- `README.md` - This file
