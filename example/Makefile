# Makefile for JSON library example
# ??????

# ?????
CXX = g++
CXXFLAGS = -std=c++17 -Wall -O2 -I..
LDFLAGS = 

# ???
SOURCES = main.cpp ../json.cpp
OBJECTS = $(SOURCES:.cpp=.o)

# double-conversion ???
DOUBLE_CONVERSION_DIR = ../double-conversion
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
DOUBLE_CONVERSION_LIB = double-conversion.a

# ???????
TARGET = json_example

# ????
all: $(TARGET)

# ?????????
$(TARGET): main.o ../json.o $(DOUBLE_CONVERSION_LIB)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

# ?????
main.o: main.cpp ../json.h
	$(CXX) $(CXXFLAGS) -c $< -o $@

# ?? json.cpp
../json.o: ../json.cpp ../json.h
	$(CXX) $(CXXFLAGS) -c $< -o $@

# ?? double-conversion ???
$(DOUBLE_CONVERSION_LIB): $(DOUBLE_CONVERSION_OBJECTS)
	$(AR) rcs $@ $^

# ?? double-conversion ???
$(DOUBLE_CONVERSION_DIR)/%.o: $(DOUBLE_CONVERSION_DIR)/%.cc
	$(CXX) $(CXXFLAGS) -c $< -o $@

# ????
run: $(TARGET)
	./$(TARGET)

# ???????
clean:
	rm -f $(TARGET) main.o ../json.o
	rm -f $(DOUBLE_CONVERSION_OBJECTS) $(DOUBLE_CONVERSION_LIB)

# ??????????? json.o?
distclean: clean
	rm -f ../json.o

.PHONY: all run clean distclean
