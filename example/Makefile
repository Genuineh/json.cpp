# Makefile for JSON library example
#
# Build the example program demonstrating basic usage of the jt::Json library

# Compiler and flags
CXX = g++
CXXFLAGS = -std=c++11 -O2 -Wall -Wextra
INCLUDES = -I..
LDFLAGS = 

# Directories
EXAMPLE_DIR = .
ROOT_DIR = ..

# Source files
SOURCES = main.cpp
JSON_SOURCES = $(ROOT_DIR)/json.cpp

# Object files
OBJECTS = main.o json.o

# double-conversion library objects
DOUBLE_CONVERSION_DIR = $(ROOT_DIR)/double-conversion
DOUBLE_CONVERSION_SOURCES = \
	$(DOUBLE_CONVERSION_DIR)/bignum.cc \
	$(DOUBLE_CONVERSION_DIR)/bignum-dtoa.cc \
	$(DOUBLE_CONVERSION_DIR)/cached-powers.cc \
	$(DOUBLE_CONVERSION_DIR)/double-to-string.cc \
	$(DOUBLE_CONVERSION_DIR)/fast-dtoa.cc \
	$(DOUBLE_CONVERSION_DIR)/fixed-dtoa.cc \
	$(DOUBLE_CONVERSION_DIR)/string-to-double.cc \
	$(DOUBLE_CONVERSION_DIR)/strtod.cc

DOUBLE_CONVERSION_OBJECTS = $(DOUBLE_CONVERSION_SOURCES:.cc=.o)

# Target executable
TARGET = json_example

# Default target
all: $(TARGET)

# Link the example executable
$(TARGET): $(OBJECTS) $(DOUBLE_CONVERSION_OBJECTS)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $(TARGET) $^

# Compile example main.cpp
main.o: $(EXAMPLE_DIR)/main.cpp $(ROOT_DIR)/json.h
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $(EXAMPLE_DIR)/main.cpp -o main.o

# Compile json.cpp
json.o: $(JSON_SOURCES) $(ROOT_DIR)/json.h
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $(JSON_SOURCES) -o json.o

# Compile double-conversion source files
$(DOUBLE_CONVERSION_DIR)/%.o: $(DOUBLE_CONVERSION_DIR)/%.cc
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

# Run the example
run: $(TARGET)
	./$(TARGET)

# Clean build artifacts
clean:
	rm -f $(TARGET) $(OBJECTS) $(DOUBLE_CONVERSION_OBJECTS)
	rm -f *.o *.a
	rm -rf build

.PHONY: all clean run
