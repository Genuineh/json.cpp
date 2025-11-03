// -*- mode:c++;indent-tabs-mode:nil;c-basic-offset:4;coding:utf-8 -*-
// vi: set et ft=cpp ts=4 sts=4 sw=4 fenc=utf-8 :vi
//
// Copyright 2024 Mozilla Foundation
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "json.h"
#include "jtckdint.h"

#include <algorithm>
#include <cassert>
#include <cctype>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <type_traits>
#include <unordered_map>

#include "double-conversion/double-to-string.h"
#include "double-conversion/string-to-double.h"

#define KEY 1
#define COMMA 2
#define COLON 4
#define ARRAY 8
#define OBJECT 16
#define DEPTH 20

#define ASCII 0
#define C0 1
#define DQUOTE 2
#define BACKSLASH 3
#define UTF8_2 4
#define UTF8_3 5
#define UTF8_4 6
#define C1 7
#define UTF8_3_E0 8
#define UTF8_3_ED 9
#define UTF8_4_F0 10
#define BADUTF8 11
#define EVILUTF8 12

#define UTF16_MASK 0xfc00
#define UTF16_MOAR 0xd800 // 0xD800..0xDBFF
#define UTF16_CONT 0xdc00 // 0xDC00..0xDFFF

#define READ32LE(S) \
    ((uint_least32_t)(255 & (S)[3]) << 030 | \
     (uint_least32_t)(255 & (S)[2]) << 020 | \
     (uint_least32_t)(255 & (S)[1]) << 010 | \
     (uint_least32_t)(255 & (S)[0]) << 000)

#define ThomPikeCont(x) (0200 == (0300 & (x)))
#define ThomPikeByte(x) ((x) & (((1 << ThomPikeMsb(x)) - 1) | 3))
#define ThomPikeLen(x) (7 - ThomPikeMsb(x))
#define ThomPikeMsb(x) ((255 & (x)) < 252 ? Bsr(255 & ~(x)) : 1)
#define ThomPikeMerge(x, y) ((x) << 6 | (077 & (y)))

#define IsSurrogate(wc) ((0xf800 & (wc)) == 0xd800)
#define IsHighSurrogate(wc) (((wc) & UTF16_MASK) == UTF16_MOAR)
#define IsLowSurrogate(wc) (((wc) & UTF16_MASK) == UTF16_CONT)
#define MergeUtf16(hi, lo) ((((hi) - 0xD800) << 10) + ((lo) - 0xDC00) + 0x10000)
#define EncodeUtf16(wc) \
    ((0x0000 <= (wc) && (wc) <= 0xFFFF) || (0xE000 <= (wc) && (wc) <= 0xFFFF) \
       ? (wc) \
     : 0x10000 <= (wc) && (wc) <= 0x10FFFF \
       ? (((((wc) - 0x10000) >> 10) + 0xD800) | \
          (unsigned)((((wc) - 0x10000) & 1023) + 0xDC00) << 16) \
       : 0xFFFD)

#if defined(__cpp_exceptions) || defined(__EXCEPTIONS) || defined(_CPPUNWIND)
#define ON_LOGIC_ERROR(s) throw std::logic_error(s)
#else
#define ON_LOGIC_ERROR(s) abort()
#endif

namespace jt {

static const char kJsonStr[256] = {
    1,  1,  1,  1,  1,  1,  1,  1, // 0000 ascii (0)
    1,  1,  1,  1,  1,  1,  1,  1, // 0010
    1,  1,  1,  1,  1,  1,  1,  1, // 0020 c0 (1)
    1,  1,  1,  1,  1,  1,  1,  1, // 0030
    0,  0,  2,  0,  0,  0,  0,  0, // 0040 dquote (2)
    0,  0,  0,  0,  0,  0,  0,  0, // 0050
    0,  0,  0,  0,  0,  0,  0,  0, // 0060
    0,  0,  0,  0,  0,  0,  0,  0, // 0070
    0,  0,  0,  0,  0,  0,  0,  0, // 0100
    0,  0,  0,  0,  0,  0,  0,  0, // 0110
    0,  0,  0,  0,  0,  0,  0,  0, // 0120
    0,  0,  0,  0,  3,  0,  0,  0, // 0130 backslash (3)
    0,  0,  0,  0,  0,  0,  0,  0, // 0140
    0,  0,  0,  0,  0,  0,  0,  0, // 0150
    0,  0,  0,  0,  0,  0,  0,  0, // 0160
    0,  0,  0,  0,  0,  0,  0,  0, // 0170
    7,  7,  7,  7,  7,  7,  7,  7, // 0200 c1 (8)
    7,  7,  7,  7,  7,  7,  7,  7, // 0210
    7,  7,  7,  7,  7,  7,  7,  7, // 0220
    7,  7,  7,  7,  7,  7,  7,  7, // 0230
    11, 11, 11, 11, 11, 11, 11, 11, // 0240 latin1 (4)
    11, 11, 11, 11, 11, 11, 11, 11, // 0250
    11, 11, 11, 11, 11, 11, 11, 11, // 0260
    11, 11, 11, 11, 11, 11, 11, 11, // 0270
    12, 12, 4,  4,  4,  4,  4,  4, // 0300 utf8-2 (5)
    4,  4,  4,  4,  4,  4,  4,  4, // 0310
    4,  4,  4,  4,  4,  4,  4,  4, // 0320 utf8-2
    4,  4,  4,  4,  4,  4,  4,  4, // 0330
    8,  5,  5,  5,  5,  5,  5,  5, // 0340 utf8-3 (6)
    5,  5,  5,  5,  5,  9,  5,  5, // 0350
    10, 6,  6,  6,  6,  11, 11, 11, // 0360 utf8-4 (7)
    11, 11, 11, 11, 11, 11, 11, 11, // 0370
};

static const char kEscapeLiteral[128] = {
    9, 9, 9, 9, 9, 9, 9, 9, 9, 1, 2, 9, 4, 3, 9, 9, // 0x00
    9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, // 0x10
    0, 0, 7, 0, 0, 0, 9, 9, 0, 0, 0, 0, 0, 0, 0, 6, // 0x20
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 9, 9, 9, 0, // 0x30
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // 0x40
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 5, 0, 0, 0, // 0x50
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // 0x60
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 9, // 0x70
};

alignas(signed char) static const signed char kHexToInt[256] = {
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, // 0x00
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, // 0x10
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, // 0x20
    0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  -1, -1, -1, -1, -1, -1, // 0x30
    -1, 10, 11, 12, 13, 14, 15, -1, -1, -1, -1, -1, -1, -1, -1, -1, // 0x40
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, // 0x50
    -1, 10, 11, 12, 13, 14, 15, -1, -1, -1, -1, -1, -1, -1, -1, -1, // 0x60
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, // 0x70
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, // 0x80
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, // 0x90
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, // 0xa0
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, // 0xb0
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, // 0xc0
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, // 0xd0
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, // 0xe0
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, // 0xf0
};

static const double_conversion::DoubleToStringConverter kDoubleToJson(
  double_conversion::DoubleToStringConverter::UNIQUE_ZERO |
    double_conversion::DoubleToStringConverter::EMIT_POSITIVE_EXPONENT_SIGN,
  "1e5000",
  "null",
  'e',
  -6,
  21,
  6,
  0);

static const double_conversion::StringToDoubleConverter kJsonToDouble(
  double_conversion::StringToDoubleConverter::ALLOW_CASE_INSENSITIVITY |
    double_conversion::StringToDoubleConverter::ALLOW_LEADING_SPACES |
    double_conversion::StringToDoubleConverter::ALLOW_TRAILING_JUNK |
    double_conversion::StringToDoubleConverter::ALLOW_TRAILING_SPACES,
  0.0,
  1.0,
  "Infinity",
  "NaN");

#if defined(__GNUC__) || defined(__clang__)
#define Bsr(x) (__builtin_clz(x) ^ (sizeof(int) * CHAR_BIT - 1))
#else
static int
Bsr(int x)
{
    int r = 0;
    if (x & 0xFFFF0000u) {
        x >>= 16;
        r |= 16;
    }
    if (x & 0xFF00) {
        x >>= 8;
        r |= 8;
    }
    if (x & 0xF0) {
        x >>= 4;
        r |= 4;
    }
    if (x & 0xC) {
        x >>= 2;
        r |= 2;
    }
    if (x & 0x2) {
        r |= 1;
    }
    return r;
}
#endif

static double
StringToDouble(const char* s, size_t n, int* out_processed)
{
    if (n == (size_t)-1)
        n = strlen(s);
    int processed;
    double res = kJsonToDouble.StringToDouble(s, n, &processed);
    if (out_processed)
        *out_processed = processed;
    return res;
}

static char*
UlongToString(char* p, unsigned long long x)
{
    char t;
    size_t i, a, b;
    i = 0;
    do {
        p[i++] = x % 10 + '0';
        x = x / 10;
    } while (x > 0);
    p[i] = '\0';
    if (i) {
        for (a = 0, b = i - 1; a < b; ++a, --b) {
            t = p[a];
            p[a] = p[b];
            p[b] = t;
        }
    }
    return p + i;
}

static char*
LongToString(char* p, long long x)
{
    if (x < 0)
        *p++ = '-', x = 0 - (unsigned long long)x;
    return UlongToString(p, x);
}

Json::Json(unsigned long value)
{
    if (value <= LLONG_MAX) {
        type_ = Long;
        long_value = value;
    } else {
        type_ = Double;
        double_value = value;
    }
}

Json::Json(unsigned long long value)
{
    if (value <= LLONG_MAX) {
        type_ = Long;
        long_value = value;
    } else {
        type_ = Double;
        double_value = value;
    }
}

Json::Json(const char* value)
{
    if (value) {
        type_ = String;
        new (&string_value) std::string(value);
    } else {
        type_ = Null;
    }
}

Json::Json(const std::string& value) : type_(String), string_value(value)
{
}

Json::~Json()
{
    if (type_ >= String)
        clear();
}

void
Json::clear()
{
    switch (type_) {
        case String:
            string_value.~basic_string();
            break;
        case Array:
            array_value.~vector();
            break;
        case Object:
            object_value.~map();
            break;
        default:
            break;
    }
    type_ = Null;
}

Json::Json(const Json& other) : type_(other.type_)
{
    switch (type_) {
        case Null:
            break;
        case Bool:
            bool_value = other.bool_value;
            break;
        case Long:
            long_value = other.long_value;
            break;
        case Float:
            float_value = other.float_value;
            break;
        case Double:
            double_value = other.double_value;
            break;
        case String:
            new (&string_value) std::string(other.string_value);
            break;
        case Array:
            new (&array_value) std::vector<Json>(other.array_value);
            break;
        case Object:
            new (&object_value) std::map<std::string, Json>(other.object_value);
            break;
        default:
            ON_LOGIC_ERROR("Unhandled JSON type.");
    }
}

Json&
Json::operator=(const Json& other)
{
    if (this != &other) {
        if (type_ >= String)
            clear();
        type_ = other.type_;
        switch (type_) {
            case Null:
                break;
            case Bool:
                bool_value = other.bool_value;
                break;
            case Long:
                long_value = other.long_value;
                break;
            case Float:
                float_value = other.float_value;
                break;
            case Double:
                double_value = other.double_value;
                break;
            case String:
                new (&string_value) std::string(other.string_value);
                break;
            case Array:
                new (&array_value) std::vector<Json>(other.array_value);
                break;
            case Object:
                new (&object_value)
                  std::map<std::string, Json>(other.object_value);
                break;
            default:
                ON_LOGIC_ERROR("Unhandled JSON type.");
        }
    }
    return *this;
}

Json::Json(Json&& other) : type_(other.type_)
{
    switch (type_) {
        case Null:
            break;
        case Bool:
            bool_value = other.bool_value;
            break;
        case Long:
            long_value = other.long_value;
            break;
        case Float:
            float_value = other.float_value;
            break;
        case Double:
            double_value = other.double_value;
            break;
        case String:
            new (&string_value) std::string(std::move(other.string_value));
            break;
        case Array:
            new (&array_value) std::vector<Json>(std::move(other.array_value));
            break;
        case Object:
            new (&object_value)
              std::map<std::string, Json>(std::move(other.object_value));
            break;
        default:
            ON_LOGIC_ERROR("Unhandled JSON type.");
    }
    other.type_ = Null;
}

Json&
Json::operator=(Json&& other)
{
    if (this != &other) {
        if (type_ >= String)
            clear();
        type_ = other.type_;
        switch (type_) {
            case Null:
                break;
            case Bool:
                bool_value = other.bool_value;
                break;
            case Long:
                long_value = other.long_value;
                break;
            case Float:
                float_value = other.float_value;
                break;
            case Double:
                double_value = other.double_value;
                break;
            case String:
                new (&string_value) std::string(std::move(other.string_value));
                break;
            case Array:
                new (&array_value)
                  std::vector<Json>(std::move(other.array_value));
                break;
            case Object:
                new (&object_value)
                  std::map<std::string, Json>(std::move(other.object_value));
                break;
            default:
                ON_LOGIC_ERROR("Unhandled JSON type.");;
        }
        other.type_ = Null;
    }
    return *this;
}

double
Json::getNumber() const
{
    switch (type_) {
        case Long:
            return long_value;
        case Float:
            return float_value;
        case Double:
            return double_value;
        default:
            ON_LOGIC_ERROR("JSON value is not a number.");
    }
}

long long
Json::getLong() const
{
    switch (type_) {
        case Long:
            return long_value;
        default:
            ON_LOGIC_ERROR("JSON value is not a long.");
    }
}

bool
Json::getBool() const
{
    switch (type_) {
        case Bool:
            return bool_value;
        default:
            ON_LOGIC_ERROR("JSON value is not a bool.");
    }
}

float
Json::getFloat() const
{
    switch (type_) {
        case Float:
            return float_value;
        case Double:
            return double_value;
        default:
            ON_LOGIC_ERROR("JSON value is not a floating-point number.");
    }
}

double
Json::getDouble() const
{
    switch (type_) {
        case Float:
            return float_value;
        case Double:
            return double_value;
        default:
            ON_LOGIC_ERROR("JSON value is not a floating-point number.");
    }
}

std::string&
Json::getString()
{
    switch (type_) {
        case String:
            return string_value;
        default:
            ON_LOGIC_ERROR("JSON value is not a string.");
    }
}

const std::string&
Json::getString() const
{
    switch (type_) {
        case String:
            return string_value;
        default:
            ON_LOGIC_ERROR("JSON value is not a string.");
    }
}

std::vector<Json>&
Json::getArray()
{
    switch (type_) {
        case Array:
            return array_value;
        default:
            ON_LOGIC_ERROR("JSON value is not an array.");
    }
}

const std::vector<Json>&
Json::getArray() const
{
    switch (type_) {
        case Array:
            return array_value;
        default:
            ON_LOGIC_ERROR("JSON value is not an array.");
    }
}

std::map<std::string, Json>&
Json::getObject()
{
    switch (type_) {
        case Object:
            return object_value;
        default:
            ON_LOGIC_ERROR("JSON value is not an object.");
    }
}

const std::map<std::string, Json>&
Json::getObject() const
{
    switch (type_) {
        case Object:
            return object_value;
        default:
            ON_LOGIC_ERROR("JSON value is not an object.");
    }
}

void
Json::setArray()
{
    if (type_ >= String)
        clear();
    type_ = Array;
    new (&array_value) std::vector<Json>();
}

void
Json::setObject()
{
    if (type_ >= String)
        clear();
    type_ = Object;
    new (&object_value) std::map<std::string, Json>();
}

bool
Json::contains(const std::string& key) const
{
    if (!isObject())
        return false;
    return object_value.find(key) != object_value.end();
}

Json&
Json::operator[](size_t index)
{
    if (!isArray())
        setArray();
    if (index >= array_value.size()) {
        array_value.resize(index + 1);
    }
    return array_value[index];
}

Json&
Json::operator[](const std::string& key)
{
    if (!isObject())
        setObject();
    return object_value[key];
}

std::string
Json::toString() const
{
    std::string b;
    marshal(b, false, 0);
    return b;
}

std::string
Json::toStringPretty() const
{
    std::string b;
    marshal(b, true, 0);
    return b;
}

void
Json::marshal(std::string& b, bool pretty, int indent) const
{
    switch (type_) {
        case Null:
            b += "null";
            break;
        case String:
            stringify(b, string_value);
            break;
        case Bool:
            b += bool_value ? "true" : "false";
            break;
        case Long: {
            char buf[64];
            b.append(buf, LongToString(buf, long_value) - buf);
            break;
        }
        case Float: {
            char buf[128];
            double_conversion::StringBuilder db(buf, 128);
            kDoubleToJson.ToShortestSingle(float_value, &db);
            db.Finalize();
            b += buf;
            break;
        }
        case Double: {
            char buf[128];
            double_conversion::StringBuilder db(buf, 128);
            kDoubleToJson.ToShortest(double_value, &db);
            db.Finalize();
            b += buf;
            break;
        }
        case Array: {
            bool once = false;
            b += '[';
            for (auto i = array_value.begin(); i != array_value.end(); ++i) {
                if (once) {
                    b += ',';
                    if (pretty)
                        b += ' ';
                } else {
                    once = true;
                }
                i->marshal(b, pretty, indent);
            }
            b += ']';
            break;
        }
        case Object: {
            bool once = false;
            b += '{';
            for (auto i = object_value.begin(); i != object_value.end(); ++i) {
                if (once) {
                    b += ',';
                } else {
                    once = true;
                }
                if (pretty && object_value.size() > 1) {
                    b += '\n';
                    ++indent;
                    for (int j = 0; j < indent; ++j)
                        b += "  ";
                }
                stringify(b, i->first);
                b += ':';
                if (pretty)
                    b += ' ';
                i->second.marshal(b, pretty, indent);
                if (pretty && object_value.size() > 1)
                    --indent;
            }
            if (pretty && object_value.size() > 1) {
                b += '\n';
                for (int j = 0; j < indent; ++j)
                    b += "  ";
                ++indent;
            }
            b += '}';
            break;
        }
        default:
            ON_LOGIC_ERROR("Unhandled JSON type.");
    }
}

void
Json::stringify(std::string& b, const std::string& s)
{
    b += '"';
    serialize(b, s);
    b += '"';
}

void
Json::serialize(std::string& sb, const std::string& s)
{
    size_t i, j, m;
    wint_t x, a, b;
    unsigned long long w;
    for (i = 0; i < s.size();) {
        x = s[i++] & 255;
        if (x >= 0300) {
            a = ThomPikeByte(x);
            m = ThomPikeLen(x) - 1;
            if (i + m <= s.size()) {
                for (j = 0;;) {
                    b = s[i + j] & 0xff;
                    if (!ThomPikeCont(b))
                        break;
                    a = ThomPikeMerge(a, b);
                    if (++j == m) {
                        x = a;
                        i += j;
                        break;
                    }
                }
            }
        }
        switch (0 <= x && x <= 127 ? kEscapeLiteral[x] : 9) {
            case 0:
                sb += x;
                break;
            case 1:
                sb += "\\t";
                break;
            case 2:
                sb += "\\n";
                break;
            case 3:
                sb += "\\r";
                break;
            case 4:
                sb += "\\f";
                break;
            case 5:
                sb += "\\\\";
                break;
            case 6:
                sb += "\\/";
                break;
            case 7:
                sb += "\\\"";
                break;
            case 9:
                w = EncodeUtf16(x);
                do {
                    char esc[6];
                    esc[0] = '\\';
                    esc[1] = 'u';
                    esc[2] = "0123456789abcdef"[(w & 0xF000) >> 014];
                    esc[3] = "0123456789abcdef"[(w & 0x0F00) >> 010];
                    esc[4] = "0123456789abcdef"[(w & 0x00F0) >> 004];
                    esc[5] = "0123456789abcdef"[(w & 0x000F) >> 000];
                    sb.append(esc, 6);
                } while ((w >>= 16));
                break;
            default:
                ON_LOGIC_ERROR("Unhandled character escape code during string serialization.");
        }
    }
}

Json::Status
Json::parse(Json& json, const char*& p, const char* e, int context, int depth)
{
    char w[4];
    long long x;
    const char* a;
    int A, B, C, D, c, d, i, u;
    if (!depth)
        return depth_exceeded;
    for (a = p, d = +1; p < e;) {
        switch ((c = *p++ & 255)) {
            case ' ': // spaces
            case '\n':
            case '\r':
            case '\t':
                a = p;
                break;

            case ',': // present in list and object
                if (context & COMMA) {
                    context = 0;
                    a = p;
                    break;
                } else {
                    return unexpected_comma;
                }

            case ':': // present only in object after key
                if (context & COLON) {
                    context = 0;
                    a = p;
                    break;
                } else {
                    return unexpected_colon;
                }

            case 'n': // null
                if (context & (KEY | COLON | COMMA))
                    goto OnColonCommaKey;
                if (p + 3 <= e && READ32LE(p - 1) == READ32LE("null")) {
                    p += 3;
                    return success;
                } else {
                    return illegal_character;
                }

            case 'f': // false
                if (context & (KEY | COLON | COMMA))
                    goto OnColonCommaKey;
                if (p + 4 <= e && READ32LE(p) == READ32LE("alse")) {
                    json.type_ = Bool;
                    json.bool_value = false;
                    p += 4;
                    return success;
                } else {
                    return illegal_character;
                }

            case 't': // true
                if (context & (KEY | COLON | COMMA))
                    goto OnColonCommaKey;
                if (p + 3 <= e && READ32LE(p - 1) == READ32LE("true")) {
                    json.type_ = Bool;
                    json.bool_value = true;
                    p += 3;
                    return success;
                } else {
                    return illegal_character;
                }

            default:
                return illegal_character;

            OnColonCommaKey:
                if (context & KEY)
                    return object_key_must_be_string;
            OnColonComma:
                if (context & COLON)
                    return missing_colon;
                return missing_comma;

            case '-': // negative
                if (context & (COLON | COMMA | KEY))
                    goto OnColonCommaKey;
                if (p < e && isdigit(*p)) {
                    d = -1;
                    break;
                } else {
                    return bad_negative;
                }

            case '0': // zero or number
                if (context & (COLON | COMMA | KEY))
                    goto OnColonCommaKey;
                if (p < e) {
                    if (*p == '.') {
                        if (p + 1 == e || !isdigit(p[1]))
                            return bad_double;
                        goto UseDubble;
                    } else if (*p == 'e' || *p == 'E') {
                        goto UseDubble;
                    } else if (isdigit(*p)) {
                        return unexpected_octal;
                    }
                }
                json.type_ = Long;
                json.long_value = 0;
                return success;

            case '1':
            case '2':
            case '3':
            case '4':
            case '5':
            case '6':
            case '7':
            case '8':
            case '9': // integer
                if (context & (COLON | COMMA | KEY))
                    goto OnColonCommaKey;
                for (x = (c - '0') * d; p < e; ++p) {
                    c = *p & 255;
                    if (isdigit(c)) {
                        if (ckd_mul(&x, x, 10) ||
                            ckd_add(&x, x, (c - '0') * d)) {
                            goto UseDubble;
                        }
                    } else if (c == '.') {
                        if (p + 1 == e || !isdigit(p[1]))
                            return bad_double;
                        goto UseDubble;
                    } else if (c == 'e' || c == 'E') {
                        goto UseDubble;
                    } else {
                        break;
                    }
                }
                json.type_ = Long;
                json.long_value = x;
                return success;

            UseDubble: // number
                json.type_ = Double;
                json.double_value = StringToDouble(a, e - a, &c);
                if (c <= 0)
                    return bad_double;
                if (a + c < e && (a[c] == 'e' || a[c] == 'E'))
                    return bad_exponent;
                p = a + c;
                return success;

            case '[': { // Array
                if (context & (COLON | COMMA | KEY))
                    goto OnColonCommaKey;
                json.setArray();
                Json value;
                for (context = ARRAY, i = 0;;) {
                    Status status = parse(value, p, e, context, depth - 1);
                    if (status == absent_value)
                        return success;
                    if (status != success)
                        return status;
                    json.array_value.emplace_back(std::move(value));
                    context = ARRAY | COMMA;
                }
            }

            case ']':
                if (context & ARRAY)
                    return absent_value;
                return unexpected_end_of_array;

            case '}':
                if (context & OBJECT)
                    return absent_value;
                return unexpected_end_of_object;

            case '{': { // Object
                if (context & (COLON | COMMA | KEY))
                    goto OnColonCommaKey;
                json.setObject();
                context = KEY | OBJECT;
                Json key, value;
                for (;;) {
                    Status status = parse(key, p, e, context, depth - 1);
                    if (status == absent_value)
                        return success;
                    if (status != success)
                        return status;
                    if (!key.isString())
                        return object_key_must_be_string;
                    status = parse(value, p, e, COLON, depth - 1);
                    if (status == absent_value)
                        return object_missing_value;
                    if (status != success)
                        return status;
                    json.object_value.emplace(std::move(key.string_value),
                                              std::move(value));
                    context = KEY | COMMA | OBJECT;
                    key.clear();
                }
            }

            case '"': { // string
                std::string b;
                if (context & (COLON | COMMA))
                    goto OnColonComma;
                for (;;) {
                    if (p >= e)
                        return unexpected_end_of_string;
                    switch (kJsonStr[(c = *p++ & 255)]) {

                        case ASCII:
                            b += c;
                            break;

                        case DQUOTE:
                            json.type_ = String;
                            new (&json.string_value) std::string(std::move(b));
                            return success;

                        case BACKSLASH:
                            if (p >= e)
                                return unexpected_end_of_string;
                            switch ((c = *p++ & 255)) {
                                case '"':
                                case '/':
                                case '\\':
                                    b += c;
                                    break;
                                case 'b':
                                    b += '\b';
                                    break;
                                case 'f':
                                    b += '\f';
                                    break;
                                case 'n':
                                    b += '\n';
                                    break;
                                case 'r':
                                    b += '\r';
                                    break;
                                case 't':
                                    b += '\t';
                                    break;
                                case 'x':
                                    if (p + 2 <= e && //
                                        (A = kHexToInt[p[0] & 255]) !=
                                          -1 && // HEX
                                        (B = kHexToInt[p[1] & 255]) != -1) { //
                                        c = A << 4 | B;
                                        if (!(0x20 <= c && c <= 0x7E))
                                            return hex_escape_not_printable;
                                        p += 2;
                                        b += c;
                                        break;
                                    } else {
                                        return invalid_hex_escape;
                                    }
                                case 'u':
                                    if (p + 4 <= e && //
                                        (A = kHexToInt[p[0] & 255]) != -1 && //
                                        (B = kHexToInt[p[1] & 255]) !=
                                          -1 && // UCS-2
                                        (C = kHexToInt[p[2] & 255]) != -1 && //
                                        (D = kHexToInt[p[3] & 255]) != -1) { //
                                        c = A << 12 | B << 8 | C << 4 | D;
                                        if (!IsSurrogate(c)) {
                                            p += 4;
                                        } else if (IsHighSurrogate(c)) {
                                            if (p + 4 + 6 <= e && //
                                                p[4] == '\\' && //
                                                p[5] == 'u' && //
                                                (A = kHexToInt[p[6] & 255]) !=
                                                  -1 && // UTF-16
                                                (B = kHexToInt[p[7] & 255]) !=
                                                  -1 && //
                                                (C = kHexToInt[p[8] & 255]) !=
                                                  -1 && //
                                                (D = kHexToInt[p[9] & 255]) !=
                                                  -1) { //
                                                u =
                                                  A << 12 | B << 8 | C << 4 | D;
                                                if (IsLowSurrogate(u)) {
                                                    p += 4 + 6;
                                                    c = MergeUtf16(c, u);
                                                } else {
                                                    goto BadUnicode;
                                                }
                                            } else {
                                                goto BadUnicode;
                                            }
                                        } else {
                                            goto BadUnicode;
                                        }
                                        // UTF-8
                                    EncodeUtf8:
                                        if (c <= 0x7f) {
                                            w[0] = c;
                                            i = 1;
                                        } else if (c <= 0x7ff) {
                                            w[0] = 0300 | (c >> 6);
                                            w[1] = 0200 | (c & 077);
                                            i = 2;
                                        } else if (c <= 0xffff) {
                                            if (IsSurrogate(c)) {
                                            ReplacementCharacter:
                                                c = 0xfffd;
                                            }
                                            w[0] = 0340 | (c >> 12);
                                            w[1] = 0200 | ((c >> 6) & 077);
                                            w[2] = 0200 | (c & 077);
                                            i = 3;
                                        } else if (~(c >> 18) & 007) {
                                            w[0] = 0360 | (c >> 18);
                                            w[1] = 0200 | ((c >> 12) & 077);
                                            w[2] = 0200 | ((c >> 6) & 077);
                                            w[3] = 0200 | (c & 077);
                                            i = 4;
                                        } else {
                                            goto ReplacementCharacter;
                                        }
                                        b.append(w, i);
                                    } else {
                                        return invalid_unicode_escape;
                                    BadUnicode:
                                        // Echo invalid \uXXXX sequences
                                        // Rather than corrupting UTF-8!
                                        b += "\\u";
                                    }
                                    break;
                                default:
                                    return invalid_escape_character;
                            }
                            break;

                        case UTF8_2:
                            if (p < e && //
                                (p[0] & 0300) == 0200) { //
                                c = (c & 037) << 6 | //
                                    (p[0] & 077); //
                                p += 1;
                                goto EncodeUtf8;
                            } else {
                                return malformed_utf8;
                            }

                        case UTF8_3_E0:
                            if (p + 2 <= e && //
                                (p[0] & 0377) < 0240 && //
                                (p[0] & 0300) == 0200 && //
                                (p[1] & 0300) == 0200) {
                                return overlong_utf8_0x7ff;
                            }
                            // fallthrough

                        case UTF8_3:
                        ThreeUtf8:
                            if (p + 2 <= e && //
                                (p[0] & 0300) == 0200 && //
                                (p[1] & 0300) == 0200) { //
                                c = (c & 017) << 12 | //
                                    (p[0] & 077) << 6 | //
                                    (p[1] & 077); //
                                p += 2;
                                goto EncodeUtf8;
                            } else {
                                return malformed_utf8;
                            }

                        case UTF8_3_ED:
                            if (p + 2 <= e && //
                                (p[0] & 0377) >= 0240) { //
                                if (p + 5 <= e && //
                                    (p[0] & 0377) >= 0256 && //
                                    (p[1] & 0300) == 0200 && //
                                    (p[2] & 0377) == 0355 && //
                                    (p[3] & 0377) >= 0260 && //
                                    (p[4] & 0300) == 0200) { //
                                    A = (0355 & 017) << 12 | // CESU-8
                                        (p[0] & 077) << 6 | //
                                        (p[1] & 077); //
                                    B = (0355 & 017) << 12 | //
                                        (p[3] & 077) << 6 | //
                                        (p[4] & 077); //
                                    c = ((A - 0xDB80) << 10) + //
                                        ((B - 0xDC00) + 0x10000); //
                                    goto EncodeUtf8;
                                } else if ((p[0] & 0300) == 0200 && //
                                           (p[1] & 0300) == 0200) { //
                                    return utf16_surrogate_in_utf8;
                                } else {
                                    return malformed_utf8;
                                }
                            }
                            goto ThreeUtf8;

                        case UTF8_4_F0:
                            if (p + 3 <= e && (p[0] & 0377) < 0220 &&
                                (((uint_least32_t)(p[+2] & 0377) << 030 |
                                  (uint_least32_t)(p[+1] & 0377) << 020 |
                                  (uint_least32_t)(p[+0] & 0377) << 010 |
                                  (uint_least32_t)(p[-1] & 0377) << 000) &
                                 0xC0C0C000) == 0x80808000) {
                                return overlong_utf8_0xffff;
                            }
                            // fallthrough
                        case UTF8_4:
                            if (p + 3 <= e && //
                                ((A =
                                    ((uint_least32_t)(p[+2] & 0377) << 030 | //
                                     (uint_least32_t)(p[+1] & 0377) << 020 | //
                                     (uint_least32_t)(p[+0] & 0377) << 010 | //
                                     (uint_least32_t)(p[-1] & 0377)
                                       << 000)) & //
                                 0xC0C0C000) == 0x80808000) { //
                                A = (A & 7) << 18 | //
                                    (A & (077 << 010)) << (12 - 010) | //
                                    (A & (077 << 020)) >> -(6 - 020) | //
                                    (A & (077 << 030)) >> 030; //
                                if (A <= 0x10FFFF) {
                                    c = A;
                                    p += 3;
                                    goto EncodeUtf8;
                                } else {
                                    return utf8_exceeds_utf16_range;
                                }
                            } else {
                                return malformed_utf8;
                            }

                        case EVILUTF8:
                            if (p < e && (p[0] & 0300) == 0200)
                                return overlong_ascii;
                            // fallthrough
                        case BADUTF8:
                            return illegal_utf8_character;
                        case C0:
                            return non_del_c0_control_code_in_string;
                        case C1:
                            return c1_control_code_in_string;
                        default:
                            ON_LOGIC_ERROR("Unhandled character category during string parsing.");
                    }
                }
            }
        }
    }
    if (depth == DEPTH)
        return absent_value;
    return unexpected_eof;
}

std::pair<Json::Status, Json>
Json::parse(const std::string& s)
{
    Json::Status s2;
    std::pair<Json::Status, Json> res;
    const char* p = s.data();
    const char* e = s.data() + s.size();
    res.first = parse(res.second, p, e, 0, DEPTH);
    if (res.first == Json::success) {
        Json j2;
        s2 = parse(j2, p, e, 0, DEPTH);
        if (s2 != absent_value)
            res.first = trailing_content;
    }
    return res;
}

namespace detail {

struct JsonPathSlice
{
    bool hasStart = false;
    long long start = 0;
    bool hasEnd = false;
    long long end = 0;
    bool hasStep = false;
    long long step = 1;
};

enum class JsonPathUnionKind
{
    Name,
    Index,
    Slice,
    Wildcard
};

struct JsonPathUnionEntry
{
    JsonPathUnionKind kind = JsonPathUnionKind::Wildcard;
    std::string name;
    long long index = 0;
    JsonPathSlice slice;
};

struct FilterNode;

struct JsonPathStep
{
    enum class Kind
    {
        Name,
        Wildcard,
        Indices,
        Slice,
        Union,
        Filter
    };

    Kind kind = Kind::Wildcard;
    bool recursive = false;
    std::string name;
    std::vector<long long> indices;
    JsonPathSlice slice;
    std::vector<JsonPathUnionEntry> unionEntries;
    std::shared_ptr<FilterNode> filter;
};

struct CompiledPath
{
    bool relative = false;
    std::vector<JsonPathStep> steps;
};

struct FilterOperand
{
    enum class Type
    {
        Literal,
        Path,
        Function
    };

    struct FunctionCall
    {
        enum class Name
        {
            Length,
            Size,
            Count
        };

        Name name = Name::Length;
        std::vector<FilterOperand> args;
    };

    Type type = Type::Literal;
    Json literal;
    CompiledPath path;
    std::shared_ptr<FunctionCall> function;
};

struct FilterNode
{
    enum class Kind
    {
        Or,
        And,
        Not,
        Comparison,
        Exists
    };

    Kind kind = Kind::Exists;
    std::string comparisonOp;
    FilterOperand lhs;
    FilterOperand rhs;
    FilterOperand existsOperand;
    std::shared_ptr<FilterNode> left;
    std::shared_ptr<FilterNode> right;
};

#if defined(__GNUC__) || defined(__clang__)
inline void
prefetch(const void* ptr)
{
    __builtin_prefetch(ptr, 0, 1);
}
#else
inline void
prefetch(const void*)
{
}
#endif

constexpr size_t kPrefetchDistance = 4;

static int
hexValue(char c)
{
    if ('0' <= c && c <= '9')
        return c - '0';
    if ('a' <= c && c <= 'f')
        return 10 + (c - 'a');
    if ('A' <= c && c <= 'F')
        return 10 + (c - 'A');
    return -1;
}

static void
appendUtf8(std::string& out, unsigned int codepoint)
{
    if (codepoint <= 0x7F) {
        out.push_back(static_cast<char>(codepoint));
    } else if (codepoint <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else if (codepoint <= 0xFFFF) {
        out.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else if (codepoint <= 0x10FFFF) {
        out.push_back(static_cast<char>(0xF0 | (codepoint >> 18)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else {
        throw std::runtime_error("Unicode codepoint out of range");
    }
}

static unsigned int
parseUnicodeEscape(const std::string& text, size_t& pos)
{
    if (pos + 4 > text.size())
        throw std::runtime_error("Incomplete unicode escape sequence in JSONPath string literal");
    unsigned int value = 0;
    for (int i = 0; i < 4; ++i) {
        int hv = hexValue(text[pos + i]);
        if (hv < 0)
            throw std::runtime_error("Invalid unicode escape in JSONPath string literal");
        value = (value << 4) | static_cast<unsigned int>(hv);
    }
    pos += 4;
    return value;
}

static std::string
parseStringLiteral(const std::string& text, size_t& pos)
{
    if (pos >= text.size())
        throw std::runtime_error("Expected string literal");
    char quote = text[pos++];
    if (quote != '\'' && quote != '"')
        throw std::runtime_error("Expected quote character");
    std::string result;
    while (pos < text.size()) {
        char c = text[pos++];
        if (c == quote)
            return result;
        if (c == '\\') {
            if (pos >= text.size())
                throw std::runtime_error("Incomplete escape sequence in JSONPath string literal");
            char esc = text[pos++];
            switch (esc) {
                case '\\':
                case '"':
                case '\'':
                    result.push_back(esc);
                    break;
                case 'b':
                    result.push_back('\b');
                    break;
                case 'f':
                    result.push_back('\f');
                    break;
                case 'n':
                    result.push_back('\n');
                    break;
                case 'r':
                    result.push_back('\r');
                    break;
                case 't':
                    result.push_back('\t');
                    break;
                case 'u': {
                    unsigned int codepoint = parseUnicodeEscape(text, pos);
                    if (0xD800 <= codepoint && codepoint <= 0xDBFF) {
                        if (pos + 2 >= text.size() || text[pos] != '\\' || text[pos + 1] != 'u')
                            throw std::runtime_error("Invalid high surrogate in JSONPath string literal");
                        pos += 2;
                        unsigned int low = parseUnicodeEscape(text, pos);
                        if (!(0xDC00 <= low && low <= 0xDFFF))
                            throw std::runtime_error("Invalid low surrogate in JSONPath string literal");
                        codepoint = 0x10000 + ((codepoint - 0xD800) << 10) + (low - 0xDC00);
                    } else if (0xDC00 <= codepoint && codepoint <= 0xDFFF) {
                        throw std::runtime_error("Unexpected low surrogate in JSONPath string literal");
                    }
                    appendUtf8(result, codepoint);
                    break;
                }
                default:
                    throw std::runtime_error("Invalid escape sequence in JSONPath string literal");
            }
        } else {
            result.push_back(c);
        }
    }
    throw std::runtime_error("Unterminated string literal in JSONPath expression");
}

static void
skipQuotedString(const std::string& text, size_t& pos)
{
    if (pos >= text.size())
        throw std::runtime_error("Expected quoted string");
    char quote = text[pos++];
    if (quote != '\'' && quote != '"')
        throw std::runtime_error("Expected quote character");
    while (pos < text.size()) {
        char c = text[pos++];
        if (c == quote)
            return;
        if (c == '\\') {
            if (pos >= text.size())
                throw std::runtime_error("Incomplete escape sequence in JSONPath string literal");
            ++pos;
        }
    }
    throw std::runtime_error("Unterminated string literal in JSONPath expression");
}

class FilterExpressionParser;

class JsonPathParser
{
  public:
    explicit JsonPathParser(const std::string& input)
      : input_(input)
      , pos_(0)
    {
    }

    CompiledPath parse();

  private:
    const std::string& input_;
    size_t pos_;

    void skipWhitespace();
    bool parseSignedInteger(long long& value);
    std::string parseIdentifier();
    JsonPathStep parseSegment();
    JsonPathStep parseBracket(bool recursive);
    JsonPathUnionEntry parseBracketEntry();
    std::shared_ptr<FilterNode> parseFilterExpression(const std::string& expression);
    [[noreturn]] void error(const std::string& message) const;
};

class FilterExpressionParser
{
  public:
    explicit FilterExpressionParser(const std::string& input)
      : input_(input)
      , pos_(0)
    {
        next();
    }

    std::shared_ptr<FilterNode> parse();

  private:
    enum class TokenType
    {
        End,
        TrueLiteral,
        FalseLiteral,
        NullLiteral,
        Number,
        String,
        Path,
        Identifier,
        LParen,
        RParen,
        Not,
        And,
        Or,
        Eq,
        Ne,
        Lt,
        Le,
        Gt,
        Ge,
        Regex,
        Comma
    };

    struct Token
    {
        TokenType type = TokenType::End;
        std::string text;
        double number = 0;
    };

    const std::string& input_;
    size_t pos_;
    Token current_;

    void skipWhitespace();
    Token lex();
    void next();
    bool match(TokenType type);
    void expect(TokenType type, const char* message);
    std::shared_ptr<FilterNode> parseOr();
    std::shared_ptr<FilterNode> parseAnd();
    std::shared_ptr<FilterNode> parseNot();
    std::shared_ptr<FilterNode> parseComparison();
    FilterOperand parseOperand();
    FilterOperand parseFunctionCall(const std::string& name);
    std::string parsePathLiteral();
    [[noreturn]] void error(const std::string& message) const;
};


void
JsonPathParser::skipWhitespace()
{
    while (pos_ < input_.size() &&
           std::isspace(static_cast<unsigned char>(input_[pos_]))) {
        ++pos_;
    }
}

[[noreturn]] void
JsonPathParser::error(const std::string& message) const
{
    std::ostringstream oss;
    oss << "JSONPath parse error at position " << pos_ << ": " << message;
    throw std::runtime_error(oss.str());
}

bool
JsonPathParser::parseSignedInteger(long long& value)
{
    skipWhitespace();
    size_t start = pos_;
    if (pos_ < input_.size() && (input_[pos_] == '+' || input_[pos_] == '-'))
        ++pos_;
    size_t digitsStart = pos_;
    while (pos_ < input_.size() &&
           std::isdigit(static_cast<unsigned char>(input_[pos_]))) {
        ++pos_;
    }
    if (digitsStart == pos_) {
        pos_ = start;
        return false;
    }
    std::string number = input_.substr(start, pos_ - start);
    value = std::strtoll(number.c_str(), nullptr, 10);
    return true;
}

std::string
JsonPathParser::parseIdentifier()
{
    if (pos_ >= input_.size())
        error("Expected identifier");
    size_t start = pos_;
    char c = input_[pos_];
    if (!(std::isalpha(static_cast<unsigned char>(c)) || c == '_' || c == '$'))
        error("Invalid identifier start");
    ++pos_;
    while (pos_ < input_.size()) {
        char ch = input_[pos_];
        if (std::isalnum(static_cast<unsigned char>(ch)) || ch == '_' || ch == '-')
            ++pos_;
        else
            break;
    }
    return input_.substr(start, pos_ - start);
}

CompiledPath
JsonPathParser::parse()
{
    CompiledPath result;
    skipWhitespace();
    if (pos_ >= input_.size())
        error("Empty JSONPath expression");
    char root = input_[pos_];
    if (root == '$') {
        result.relative = false;
    } else if (root == '@') {
        result.relative = true;
    } else {
        error("JSONPath must start with '$' or '@'");
    }
    ++pos_;
    while (true) {
        skipWhitespace();
        if (pos_ >= input_.size())
            break;
        result.steps.emplace_back(parseSegment());
    }
    return result;
}

class JsonPathCache
{
  public:
    const CompiledPath& get(const std::string& expression)
    {
        const uint64_t now = ++clock_;
        auto it = cache_.find(expression);
        if (it != cache_.end()) {
            it->second.lastUsedTick = now;
            return it->second.path;
        }
        JsonPathParser parser(expression);
        CacheEntry entry;
        entry.path = parser.parse();
        entry.lastUsedTick = now;
        auto [insertedIt, inserted] = cache_.emplace(expression, std::move(entry));
        if (cache_.size() > kMaxEntries)
            evictOldest();
        return insertedIt->second.path;
    }

  private:
    struct CacheEntry
    {
        CompiledPath path;
        uint64_t lastUsedTick = 0;
    };

    static constexpr size_t kMaxEntries = 64;

    std::unordered_map<std::string, CacheEntry> cache_;
    uint64_t clock_ = 0;

    void evictOldest()
    {
        auto oldestIt = cache_.end();
        for (auto it = cache_.begin(); it != cache_.end(); ++it) {
            if (oldestIt == cache_.end() ||
                it->second.lastUsedTick < oldestIt->second.lastUsedTick)
                oldestIt = it;
        }
        if (oldestIt != cache_.end())
            cache_.erase(oldestIt);
    }
};

inline JsonPathCache&
getThreadLocalCache()
{
    thread_local JsonPathCache cache;
    return cache;
}

static const CompiledPath&
getCompiledPathCached(const std::string& expression)
{
    return getThreadLocalCache().get(expression);
}

JsonPathStep
JsonPathParser::parseSegment()
{
    skipWhitespace();
    bool recursive = false;
    if (pos_ < input_.size() && input_[pos_] == '.') {
        ++pos_;
        if (pos_ < input_.size() && input_[pos_] == '.') {
            recursive = true;
            ++pos_;
        }
    } else if (pos_ + 1 < input_.size() && input_[pos_] == '.' &&
               input_[pos_ + 1] == '.') {
        recursive = true;
        pos_ += 2;
    }
    skipWhitespace();
    if (pos_ >= input_.size())
        error("Incomplete JSONPath segment");
    if (input_[pos_] == '[')
        return parseBracket(recursive);
    if (input_[pos_] == '*') {
        ++pos_;
        JsonPathStep step;
        step.kind = JsonPathStep::Kind::Wildcard;
        step.recursive = recursive;
        return step;
    }
    std::string name = parseIdentifier();
    JsonPathStep step;
    step.kind = JsonPathStep::Kind::Name;
    step.recursive = recursive;
    step.name = std::move(name);
    return step;
}

JsonPathStep
JsonPathParser::parseBracket(bool recursive)
{
    if (input_[pos_] != '[')
        error("Expected '['");
    ++pos_;
    skipWhitespace();
    if (pos_ >= input_.size())
        error("Unterminated '[' segment");
    if (input_[pos_] == '?') {
        ++pos_;
        skipWhitespace();
        if (pos_ >= input_.size() || input_[pos_] != '(')
            error("Expected '(' after '?' in filter expression");
        ++pos_;
        size_t exprStart = pos_;
        int depth = 1;
        while (pos_ < input_.size() && depth > 0) {
            char c = input_[pos_++];
            if (c == '\'' || c == '"') {
                size_t temp = pos_ - 1;
                skipQuotedString(input_, temp);
                pos_ = temp;
            } else if (c == '(') {
                ++depth;
            } else if (c == ')') {
                --depth;
            }
        }
        if (depth != 0)
            error("Unterminated filter expression");
        size_t exprEnd = pos_ - 1;
        std::string filterExpr = input_.substr(exprStart, exprEnd - exprStart);
        skipWhitespace();
        if (pos_ >= input_.size() || input_[pos_] != ']')
            error("Expected ']' after filter expression");
        ++pos_;
        JsonPathStep step;
        step.kind = JsonPathStep::Kind::Filter;
        step.recursive = recursive;
        step.filter = parseFilterExpression(filterExpr);
        return step;
    }
    if (input_[pos_] == '*') {
        ++pos_;
        skipWhitespace();
        if (pos_ >= input_.size() || input_[pos_] != ']')
            error("Expected ']' after '*'");
        ++pos_;
        JsonPathStep step;
        step.kind = JsonPathStep::Kind::Wildcard;
        step.recursive = recursive;
        return step;
    }
    std::vector<JsonPathUnionEntry> entries;
    entries.emplace_back(parseBracketEntry());
    skipWhitespace();
    while (pos_ < input_.size() && input_[pos_] == ',') {
        ++pos_;
        skipWhitespace();
        entries.emplace_back(parseBracketEntry());
        skipWhitespace();
    }
    if (pos_ >= input_.size() || input_[pos_] != ']')
        error("Expected ']' after bracket expression");
    ++pos_;
    JsonPathStep step;
    step.recursive = recursive;
    if (entries.size() == 1) {
        const JsonPathUnionEntry& entry = entries.front();
        switch (entry.kind) {
            case JsonPathUnionKind::Name:
                step.kind = JsonPathStep::Kind::Name;
                step.name = entry.name;
                break;
            case JsonPathUnionKind::Index:
                step.kind = JsonPathStep::Kind::Indices;
                step.indices.push_back(entry.index);
                break;
            case JsonPathUnionKind::Slice:
                step.kind = JsonPathStep::Kind::Slice;
                step.slice = entry.slice;
                break;
            case JsonPathUnionKind::Wildcard:
                step.kind = JsonPathStep::Kind::Wildcard;
                break;
        }
    } else {
        step.kind = JsonPathStep::Kind::Union;
        step.unionEntries = std::move(entries);
    }
    return step;
}

JsonPathUnionEntry
JsonPathParser::parseBracketEntry()
{
    skipWhitespace();
    if (pos_ >= input_.size())
        error("Unexpected end of bracket expression");
    char c = input_[pos_];
    if (c == '\'' || c == '"') {
        std::string name = parseStringLiteral(input_, pos_);
        JsonPathUnionEntry entry;
        entry.kind = JsonPathUnionKind::Name;
        entry.name = std::move(name);
        return entry;
    }
    if (c == '*') {
        ++pos_;
        JsonPathUnionEntry entry;
        entry.kind = JsonPathUnionKind::Wildcard;
        return entry;
    }
    size_t before = pos_;
    long long number = 0;
    bool hasNumber = parseSignedInteger(number);
    skipWhitespace();
    if (pos_ < input_.size() && input_[pos_] == ':') {
        ++pos_;
        JsonPathSlice slice;
        slice.hasStart = hasNumber;
        if (slice.hasStart)
            slice.start = number;
        skipWhitespace();
        long long endValue = 0;
        bool hasEnd = parseSignedInteger(endValue);
        slice.hasEnd = hasEnd;
        if (slice.hasEnd)
            slice.end = endValue;
        skipWhitespace();
        if (pos_ < input_.size() && input_[pos_] == ':') {
            ++pos_;
            skipWhitespace();
            long long stepValue = 0;
            if (!parseSignedInteger(stepValue))
                error("Slice step expects integer");
            slice.hasStep = true;
            slice.step = stepValue;
        }
        JsonPathUnionEntry entry;
        entry.kind = JsonPathUnionKind::Slice;
        entry.slice = slice;
        return entry;
    }
    if (hasNumber) {
        JsonPathUnionEntry entry;
        entry.kind = JsonPathUnionKind::Index;
        entry.index = number;
        return entry;
    }
    pos_ = before;
    std::string name = parseIdentifier();
    JsonPathUnionEntry entry;
    entry.kind = JsonPathUnionKind::Name;
    entry.name = std::move(name);
    return entry;
}

std::shared_ptr<FilterNode>
JsonPathParser::parseFilterExpression(const std::string& expression)
{
    FilterExpressionParser parser(expression);
    return parser.parse();
}


void
FilterExpressionParser::skipWhitespace()
{
    while (pos_ < input_.size() &&
           std::isspace(static_cast<unsigned char>(input_[pos_]))) {
        ++pos_;
    }
}

[[noreturn]] void
FilterExpressionParser::error(const std::string& message) const
{
    std::ostringstream oss;
    oss << "JSONPath filter parse error at position " << pos_ << ": "
        << message;
    throw std::runtime_error(oss.str());
}

void
FilterExpressionParser::next()
{
    current_ = lex();
}

bool
FilterExpressionParser::match(TokenType type)
{
    if (current_.type == type) {
        next();
        return true;
    }
    return false;
}

void
FilterExpressionParser::expect(TokenType type, const char* message)
{
    if (!match(type))
        error(message);
}

FilterExpressionParser::Token
FilterExpressionParser::lex()
{
    skipWhitespace();
    Token token;
    if (pos_ >= input_.size()) {
        token.type = TokenType::End;
        return token;
    }
    char c = input_[pos_];
    if (c == '&' && pos_ + 1 < input_.size() && input_[pos_ + 1] == '&') {
        pos_ += 2;
        token.type = TokenType::And;
        token.text = "&&";
        return token;
    }
    if (c == '|' && pos_ + 1 < input_.size() && input_[pos_ + 1] == '|') {
        pos_ += 2;
        token.type = TokenType::Or;
        token.text = "||";
        return token;
    }
    if (c == '=' && pos_ + 1 < input_.size() && input_[pos_ + 1] == '=') {
        pos_ += 2;
        token.type = TokenType::Eq;
        token.text = "==";
        return token;
    }
    if (c == '!' && pos_ + 1 < input_.size() && input_[pos_ + 1] == '=') {
        pos_ += 2;
        token.type = TokenType::Ne;
        token.text = "!=";
        return token;
    }
    if (c == '<' && pos_ + 1 < input_.size() && input_[pos_ + 1] == '=') {
        pos_ += 2;
        token.type = TokenType::Le;
        token.text = "<=";
        return token;
    }
    if (c == '<') {
        ++pos_;
        token.type = TokenType::Lt;
        token.text = "<";
        return token;
    }

    if (c == '>' && pos_ + 1 < input_.size() && input_[pos_ + 1] == '=') {
        pos_ += 2;
        token.type = TokenType::Ge;
        token.text = ">=";
        return token;
    }
    if (c == '>') {
        ++pos_;
        token.type = TokenType::Gt;
        token.text = ">";
        return token;
    }

    if (c == '=' && pos_ + 1 < input_.size() && input_[pos_ + 1] == '~') {
        pos_ += 2;
        token.type = TokenType::Regex;
        token.text = "=~";
        return token;
    }
    switch (c) {
        case '!':
            ++pos_;
            token.type = TokenType::Not;
            token.text = "!";
            return token;
        case '(':
            ++pos_;
            token.type = TokenType::LParen;
            token.text = "(";
            return token;
        case ')':
            ++pos_;
            token.type = TokenType::RParen;
            token.text = ")";
            return token;
        case ',':
            ++pos_;
            token.type = TokenType::Comma;
            token.text = ",";
            return token;
    }
    if (c == '\'' || c == '"') {
        token.text = parseStringLiteral(input_, pos_);
        token.type = TokenType::String;
        return token;
    }
    if (c == '@' || c == '$') {
        token.text = parsePathLiteral();
        token.type = TokenType::Path;
        return token;
    }
    if (std::isdigit(static_cast<unsigned char>(c)) || c == '-' || c == '+') {
        size_t start = pos_;
        if (input_[pos_] == '-' || input_[pos_] == '+')
            ++pos_;
        bool hasDigits = false;
        while (pos_ < input_.size() &&
               std::isdigit(static_cast<unsigned char>(input_[pos_]))) {
            ++pos_;
            hasDigits = true;
        }
        if (pos_ < input_.size() && input_[pos_] == '.') {
            ++pos_;
            while (pos_ < input_.size() &&
                   std::isdigit(static_cast<unsigned char>(input_[pos_]))) {
                ++pos_;
                hasDigits = true;
            }
        }
        if (pos_ < input_.size() && (input_[pos_] == 'e' || input_[pos_] == 'E')) {
            ++pos_;
            if (pos_ < input_.size() && (input_[pos_] == '+' || input_[pos_] == '-'))
                ++pos_;
            while (pos_ < input_.size() &&
                   std::isdigit(static_cast<unsigned char>(input_[pos_]))) {
                ++pos_;
                hasDigits = true;
            }
        }
        if (!hasDigits)
            error("Invalid numeric literal in filter expression");
        token.text = input_.substr(start, pos_ - start);
        token.number = std::strtod(token.text.c_str(), nullptr);
        token.type = TokenType::Number;
        return token;
    }
    if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
        size_t start = pos_;
        while (pos_ < input_.size() &&
               (std::isalnum(static_cast<unsigned char>(input_[pos_])) ||
                input_[pos_] == '_' || input_[pos_] == '-')) {
            ++pos_;
        }
        token.text = input_.substr(start, pos_ - start);
        if (token.text == "true") {
            token.type = TokenType::TrueLiteral;
        } else if (token.text == "false") {
            token.type = TokenType::FalseLiteral;
        } else if (token.text == "null") {
            token.type = TokenType::NullLiteral;
        } else {
            token.type = TokenType::Identifier;
        }
        return token;
    }
    error("Unexpected character in filter expression");
    return token;
}

std::shared_ptr<FilterNode>
FilterExpressionParser::parse()
{
    auto node = parseOr();
    if (current_.type != TokenType::End)
        error("Unexpected token at end of filter expression");
    return node;
}

std::shared_ptr<FilterNode>
FilterExpressionParser::parseOr()
{
    auto node = parseAnd();
    while (current_.type == TokenType::Or) {
        next();
        auto rhs = parseAnd();
        auto parent = std::make_shared<FilterNode>();
        parent->kind = FilterNode::Kind::Or;
        parent->left = node;
        parent->right = rhs;
        node = parent;
    }
    return node;
}

std::shared_ptr<FilterNode>
FilterExpressionParser::parseAnd()
{
    auto node = parseNot();
    while (current_.type == TokenType::And) {
        next();
        auto rhs = parseNot();
        auto parent = std::make_shared<FilterNode>();
        parent->kind = FilterNode::Kind::And;
        parent->left = node;
        parent->right = rhs;
        node = parent;
    }
    return node;
}

std::shared_ptr<FilterNode>
FilterExpressionParser::parseNot()
{
    if (current_.type == TokenType::Not) {
        next();
        auto child = parseNot();
        auto node = std::make_shared<FilterNode>();
        node->kind = FilterNode::Kind::Not;
        node->left = child;
        return node;
    }
    return parseComparison();
}

std::shared_ptr<FilterNode>
FilterExpressionParser::parseComparison()
{
    if (current_.type == TokenType::LParen) {
        next();
        auto node = parseOr();
        expect(TokenType::RParen, "Expected ')' in filter expression");
        return node;
    }
    FilterOperand left = parseOperand();
    if (current_.type == TokenType::Eq || current_.type == TokenType::Ne ||
        current_.type == TokenType::Lt || current_.type == TokenType::Le ||
        current_.type == TokenType::Gt || current_.type == TokenType::Ge ||
        current_.type == TokenType::Regex) {
        std::string op = current_.text;
        next();
        FilterOperand right = parseOperand();
        auto node = std::make_shared<FilterNode>();
        node->kind = FilterNode::Kind::Comparison;
        node->comparisonOp = std::move(op);
        node->lhs = std::move(left);
        node->rhs = std::move(right);
        return node;
    }
    auto node = std::make_shared<FilterNode>();
    node->kind = FilterNode::Kind::Exists;
    node->existsOperand = std::move(left);
    return node;
}

FilterOperand
FilterExpressionParser::parseOperand()
{
    FilterOperand operand;
    switch (current_.type) {
        case TokenType::TrueLiteral:
            operand.type = FilterOperand::Type::Literal;
            operand.literal = Json(true);
            next();
            return operand;
        case TokenType::FalseLiteral:
            operand.type = FilterOperand::Type::Literal;
            operand.literal = Json(false);
            next();
            return operand;
        case TokenType::NullLiteral:
            operand.type = FilterOperand::Type::Literal;
            operand.literal = Json(nullptr);
            next();
            return operand;
        case TokenType::Number: {
            operand.type = FilterOperand::Type::Literal;
            if (current_.text.find_first_of(".eE") == std::string::npos) {
                long long val = std::strtoll(current_.text.c_str(), nullptr, 10);
                operand.literal = Json(val);
            } else {
                operand.literal = Json(current_.number);
            }
            next();
            return operand;
        }
        case TokenType::String:
            operand.type = FilterOperand::Type::Literal;
            operand.literal = Json(current_.text);
            next();
            return operand;
        case TokenType::Path: {
            JsonPathParser parser(current_.text);
            operand.type = FilterOperand::Type::Path;
            operand.path = parser.parse();
            next();
            return operand;
        }
        case TokenType::Identifier: {
            std::string name = current_.text;
            next();
            if (current_.type == TokenType::LParen)
                return parseFunctionCall(name);
            error("Unexpected identifier in filter expression");
        }
        default:
            error("Unexpected token in filter operand");
    }
    return operand;
}

FilterOperand
FilterExpressionParser::parseFunctionCall(const std::string& name)
{
    FilterOperand operand;
    operand.type = FilterOperand::Type::Function;
    operand.function = std::make_shared<FilterOperand::FunctionCall>();
    std::string lowered;
    lowered.resize(name.size());
    std::transform(name.begin(), name.end(), lowered.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    if (lowered == "length" || lowered == "size") {
        operand.function->name = FilterOperand::FunctionCall::Name::Length;
    } else if (lowered == "count") {
        operand.function->name = FilterOperand::FunctionCall::Name::Count;
    } else {
        error("Unsupported function in filter expression");
    }
    expect(TokenType::LParen, "Expected '(' after function name");
    if (current_.type != TokenType::RParen) {
        operand.function->args.push_back(parseOperand());
        while (current_.type == TokenType::Comma) {
            next();
            operand.function->args.push_back(parseOperand());
        }
    }
    expect(TokenType::RParen, "Expected ')' after function call");
    return operand;
}

std::string
FilterExpressionParser::parsePathLiteral()
{
    size_t start = pos_;
    int bracketDepth = 0;
    while (pos_ < input_.size()) {
        char c = input_[pos_];
        if (c == '\'' || c == '"') {
            size_t temp = pos_;
            skipQuotedString(input_, temp);
            pos_ = temp;
            continue;
        }
        if (c == '[') {
            ++bracketDepth;
            ++pos_;
            continue;
        }
        if (c == ']') {
            if (bracketDepth == 0)
                break;
            --bracketDepth;
            ++pos_;
            continue;
        }
        if (bracketDepth == 0) {
            if (std::isspace(static_cast<unsigned char>(c)) || c == ')' || c == '(' ||
                c == ',' || c == '!' || c == '=' || c == '<' || c == '>' ||
                c == '&' || c == '|') {
                break;
            }
        }
        ++pos_;
    }
    if (start == pos_)
        error("Expected path literal");
    return input_.substr(start, pos_ - start);
}

template <typename JsonType>
struct JsonAccessor;

template <>
struct JsonAccessor<Json>
{
    using Pointer = Json*;
    using ArrayType = std::vector<Json>;
    using ObjectType = std::map<std::string, Json>;

    static bool isArray(const Json& value) { return value.isArray(); }
    static bool isObject(const Json& value) { return value.isObject(); }
    static ArrayType& getArray(Json& value) { return value.getArray(); }
    static ObjectType& getObject(Json& value) { return value.getObject(); }
};

template <>
struct JsonAccessor<const Json>
{
    using Pointer = const Json*;
    using ArrayType = const std::vector<Json>;
    using ObjectType = const std::map<std::string, Json>;

    static bool isArray(const Json& value) { return value.isArray(); }
    static bool isObject(const Json& value) { return value.isObject(); }
    static const ArrayType& getArray(const Json& value) { return value.getArray(); }
    static const ObjectType& getObject(const Json& value) { return value.getObject(); }
};

static bool
normalizeIndex(long long index, size_t size, size_t& out)
{
    long long normalized = index;
    if (normalized < 0)
        normalized += static_cast<long long>(size);
    if (normalized < 0 || normalized >= static_cast<long long>(size))
        return false;
    out = static_cast<size_t>(normalized);
    return true;
}

template <typename JsonType>
static void
collectDescendants(JsonType* node,
                   std::vector<JsonType*>& out,
                   std::vector<JsonType*>& stack)
{
    stack.clear();
    stack.push_back(node);
    while (!stack.empty()) {
        JsonType* current = stack.back();
        stack.pop_back();
        out.push_back(current);
        if (current->isArray()) {
            auto& arr = JsonAccessor<JsonType>::getArray(*current);
            const size_t arrSize = arr.size();
            if (arrSize > 0) {
                out.reserve(out.size() + arrSize);
                stack.reserve(stack.size() + arrSize);
                for (size_t i = arrSize; i-- > 0;)
                    stack.push_back(&arr[i]);
            }
        } else if (current->isObject()) {
            auto& obj = JsonAccessor<JsonType>::getObject(*current);
            const size_t objSize = obj.size();
            if (objSize > 0) {
                out.reserve(out.size() + objSize);
                stack.reserve(stack.size() + objSize);
                for (auto it = obj.rbegin(); it != obj.rend(); ++it)
                    stack.push_back(&it->second);
            }
        }
    }
}

template <typename JsonType>
static void
applySlice(JsonType* node, const JsonPathSlice& slice, std::vector<JsonType*>& out)
{
    if (!node->isArray())
        return;
    auto& arr = JsonAccessor<JsonType>::getArray(*node);
    const long long size = static_cast<long long>(arr.size());
    if (size == 0)
        return;
    const long long step = slice.hasStep ? slice.step : 1;
    if (step == 0)
        throw std::runtime_error("JSONPath slice step cannot be zero");
    if (step > 0) {
        long long start = slice.hasStart ? slice.start : 0;
        long long end = slice.hasEnd ? slice.end : size;
        if (start < 0)
            start += size;
        if (end < 0)
            end += size;
        start = std::max(0LL, std::min(start, size));
        end = std::max(0LL, std::min(end, size));
        // Calculate expected capacity and reserve
        if (start < end) {
            const size_t expectedCount = static_cast<size_t>((end - start + step - 1) / step);
            out.reserve(out.size() + expectedCount);
        }
        for (long long i = start; i < end; i += step)
            out.push_back(&arr[static_cast<size_t>(i)]);
    } else {
        long long start = slice.hasStart ? slice.start : (size - 1);
        long long end = slice.hasEnd ? slice.end : -1;
        if (start < 0)
            start += size;
        if (end < 0)
            end += size;
        if (start >= size)
            start = size - 1;
        if (start < 0)
            start = -1;
        if (end >= size)
            end = size - 1;
        if (end < -1)
            end = -1;
        // Calculate expected capacity for negative step
        if (start > end) {
            const size_t expectedCount = static_cast<size_t>((start - end - step - 1) / (-step));
            out.reserve(out.size() + expectedCount);
        }
        for (long long i = start; i > end; i += step) {
            if (i >= 0 && i < size)
                out.push_back(&arr[static_cast<size_t>(i)]);
        }
    }
}

template <typename JsonType>
static void
applyUnionEntry(JsonType* node,
                const JsonPathUnionEntry& entry,
                std::vector<JsonType*>& out)
{
    switch (entry.kind) {
        case JsonPathUnionKind::Name: {
            if (!node->isObject())
                return;
            auto& obj = JsonAccessor<JsonType>::getObject(*node);
            auto it = obj.find(entry.name);
            if (it != obj.end())
                out.push_back(&it->second);
            break;
        }
        case JsonPathUnionKind::Index: {
            if (!node->isArray())
                return;
            auto& arr = JsonAccessor<JsonType>::getArray(*node);
            size_t idx;
            if (normalizeIndex(entry.index, arr.size(), idx))
                out.push_back(&arr[idx]);
            break;
        }
        case JsonPathUnionKind::Slice:
            applySlice(node, entry.slice, out);
            break;
        case JsonPathUnionKind::Wildcard: {
            if (node->isArray()) {
                auto& arr = JsonAccessor<JsonType>::getArray(*node);
                for (size_t i = 0; i < arr.size(); ++i)
                    out.push_back(&arr[i]);
            } else if (node->isObject()) {
                auto& obj = JsonAccessor<JsonType>::getObject(*node);
                for (auto it = obj.begin(); it != obj.end(); ++it)
                    out.push_back(&it->second);
            }
            break;
        }
    }
}



template <typename JsonType>
static std::vector<JsonType*>
evaluatePathInternal(JsonType* start,
                     const std::vector<JsonPathStep>& steps,
                     JsonType* documentRoot);

static std::vector<Json*>
evaluatePathMutable(Json& start,
                    const std::vector<JsonPathStep>& steps,
                    Json& documentRoot);

static std::vector<const Json*>
evaluatePathConst(const Json& start,
                  const std::vector<JsonPathStep>& steps,
                  const Json& documentRoot);
struct EvaluatedOperand
{
    std::vector<const Json*> nodes;
    std::vector<std::shared_ptr<Json>> owned;

    void addOwned(Json value)
    {
        auto ptr = std::make_shared<Json>(std::move(value));
        nodes.push_back(ptr.get());
        owned.push_back(std::move(ptr));
    }
};

class FilterEvaluator
{
  public:
    static bool evaluate(const std::shared_ptr<FilterNode>& node,
                         const Json& documentRoot,
                         const Json& context);

  private:
    static EvaluatedOperand evaluateOperand(const FilterOperand& operand,
                                            const Json& documentRoot,
                                            const Json& context);
    static std::vector<const Json*> evaluatePath(const CompiledPath& path,
                                                 const Json& documentRoot,
                                                 const Json& context);
    static Json evaluateFunction(const FilterOperand::FunctionCall& fn,
                                 const Json& documentRoot,
                                 const Json& context);
    static bool compare(const std::string& op,
                        const EvaluatedOperand& lhs,
                        const EvaluatedOperand& rhs);
    static bool equalsAny(const EvaluatedOperand& lhs,
                          const EvaluatedOperand& rhs);
    static bool notEquals(const EvaluatedOperand& lhs,
                          const EvaluatedOperand& rhs);
    static bool relational(const std::string& op,
                           const EvaluatedOperand& lhs,
                           const EvaluatedOperand& rhs);
    static bool regexMatch(const EvaluatedOperand& lhs,
                           const EvaluatedOperand& rhs);
    static bool truthy(const EvaluatedOperand& operand);
    static bool truthy(const Json& value);
    static bool toNumber(const Json& value, double& out);
    static bool toString(const Json& value, std::string& out);
    static bool jsonEquals(const Json& lhs, const Json& rhs);
    static bool compareNumbers(double lhs, double rhs, const std::string& op);
    static bool compareStrings(const std::string& lhs,
                               const std::string& rhs,
                               const std::string& op);
    static long long computeLength(const Json& value);
};

bool
FilterEvaluator::evaluate(const std::shared_ptr<FilterNode>& node,
                          const Json& documentRoot,
                          const Json& context)
{
    if (!node)
        return false;
    switch (node->kind) {
        case FilterNode::Kind::Or:
            return evaluate(node->left, documentRoot, context) ||
                   evaluate(node->right, documentRoot, context);
        case FilterNode::Kind::And:
            return evaluate(node->left, documentRoot, context) &&
                   evaluate(node->right, documentRoot, context);
        case FilterNode::Kind::Not:
            return !evaluate(node->left, documentRoot, context);
        case FilterNode::Kind::Comparison: {
            EvaluatedOperand lhs = evaluateOperand(node->lhs, documentRoot, context);
            EvaluatedOperand rhs = evaluateOperand(node->rhs, documentRoot, context);
            return compare(node->comparisonOp, lhs, rhs);
        }
        case FilterNode::Kind::Exists: {
            EvaluatedOperand lhs = evaluateOperand(node->existsOperand, documentRoot, context);
            return truthy(lhs);
        }
    }
    return false;
}

EvaluatedOperand
FilterEvaluator::evaluateOperand(const FilterOperand& operand,
                                 const Json& documentRoot,
                                 const Json& context)
{
    EvaluatedOperand result;
    switch (operand.type) {
        case FilterOperand::Type::Literal:
            result.addOwned(operand.literal);
            break;
        case FilterOperand::Type::Path: {
            auto matches = evaluatePath(operand.path, documentRoot, context);
            result.nodes.insert(result.nodes.end(), matches.begin(), matches.end());
            break;
        }
        case FilterOperand::Type::Function: {
            Json value = evaluateFunction(*operand.function, documentRoot, context);
            result.addOwned(std::move(value));
            break;
        }
    }
    return result;
}

std::vector<const Json*>
FilterEvaluator::evaluatePath(const CompiledPath& path,
                              const Json& documentRoot,
                              const Json& context)
{
    if (path.relative)
        return evaluatePathConst(context, path.steps, documentRoot);
    return evaluatePathConst(documentRoot, path.steps, documentRoot);
}

Json
FilterEvaluator::evaluateFunction(const FilterOperand::FunctionCall& fn,
                                  const Json& documentRoot,
                                  const Json& context)
{
    if (fn.args.size() != 1)
        throw std::runtime_error("Filter function expects exactly one argument");
    EvaluatedOperand arg = evaluateOperand(fn.args[0], documentRoot, context);
    const Json* target = nullptr;
    if (!arg.nodes.empty())
        target = arg.nodes.front();
    if (!target)
        return Json(0);
    switch (fn.name) {
        case FilterOperand::FunctionCall::Name::Length:
            return Json(static_cast<long long>(computeLength(*target)));
        case FilterOperand::FunctionCall::Name::Count:
            if (target->isArray())
                return Json(static_cast<long long>(target->getArray().size()));
            if (target->isObject())
                return Json(static_cast<long long>(target->getObject().size()));
            return Json(1);
        default:
            throw std::runtime_error("Unsupported filter function");
    }
}

bool
FilterEvaluator::compare(const std::string& op,
                         const EvaluatedOperand& lhs,
                         const EvaluatedOperand& rhs)
{
    if (op == "==")
        return equalsAny(lhs, rhs);
    if (op == "!=")
        return notEquals(lhs, rhs);
    if (op == "<" || op == "<=" || op == ">" || op == ">=")
        return relational(op, lhs, rhs);
    if (op == "=~")
        return regexMatch(lhs, rhs);
    return false;
}

bool
FilterEvaluator::equalsAny(const EvaluatedOperand& lhs,
                           const EvaluatedOperand& rhs)
{
    if (lhs.nodes.empty() || rhs.nodes.empty())
        return false;
    for (const Json* l : lhs.nodes) {
        for (const Json* r : rhs.nodes) {
            if (jsonEquals(*l, *r))
                return true;
        }
    }
    return false;
}

bool
FilterEvaluator::notEquals(const EvaluatedOperand& lhs,
                           const EvaluatedOperand& rhs)
{
    if (lhs.nodes.empty())
        return false;
    if (rhs.nodes.empty())
        return true;
    for (const Json* l : lhs.nodes) {
        bool anyEqual = false;
        for (const Json* r : rhs.nodes) {
            if (jsonEquals(*l, *r)) {
                anyEqual = true;
                break;
            }
        }
        if (!anyEqual)
            return true;
    }
    return false;
}

bool
FilterEvaluator::relational(const std::string& op,
                            const EvaluatedOperand& lhs,
                            const EvaluatedOperand& rhs)
{
    if (lhs.nodes.empty() || rhs.nodes.empty())
        return false;
    for (const Json* l : lhs.nodes) {
        double left;
        std::string leftStr;
        bool leftNum = toNumber(*l, left);
        bool leftString = toString(*l, leftStr);
        for (const Json* r : rhs.nodes) {
            double right;
            std::string rightStr;
            bool rightNum = toNumber(*r, right);
            bool rightString = toString(*r, rightStr);
            if (leftNum && rightNum && compareNumbers(left, right, op))
                return true;
            if (leftString && rightString && compareStrings(leftStr, rightStr, op))
                return true;
        }
    }
    return false;
}

bool
FilterEvaluator::regexMatch(const EvaluatedOperand& lhs,
                            const EvaluatedOperand& rhs)
{
    if (lhs.nodes.empty() || rhs.nodes.empty())
        return false;
    std::string pattern;
    if (!toString(*rhs.nodes.front(), pattern))
        return false;
    try {
        std::regex re(pattern);
        for (const Json* l : lhs.nodes) {
            std::string text;
            if (toString(*l, text) && std::regex_search(text, re))
                return true;
        }
    } catch (const std::regex_error&) {
        throw std::runtime_error("Invalid regular expression in JSONPath filter");
    }
    return false;
}

bool
FilterEvaluator::truthy(const EvaluatedOperand& operand)
{
    for (const Json* node : operand.nodes) {
        if (truthy(*node))
            return true;
    }
    return false;
}

bool
FilterEvaluator::truthy(const Json& value)
{
    if (value.isNull())
        return false;
    if (value.isBool())
        return value.getBool();
    if (value.isLong())
        return value.getLong() != 0;
    if (value.isDouble() || value.isFloat())
        return value.getNumber() != 0.0;
    if (value.isString())
        return !value.getString().empty();
    if (value.isArray())
        return !value.getArray().empty();
    if (value.isObject())
        return !value.getObject().empty();
    return false;
}

bool
FilterEvaluator::toNumber(const Json& value, double& out)
{
    if (value.isLong()) {
        out = static_cast<double>(value.getLong());
        return true;
    }
    if (value.isDouble() || value.isFloat()) {
        out = value.getNumber();
        return true;
    }
    if (value.isBool()) {
        out = value.getBool() ? 1.0 : 0.0;
        return true;
    }
    return false;
}

bool
FilterEvaluator::toString(const Json& value, std::string& out)
{
    if (value.isString()) {
        out = value.getString();
        return true;
    }
    return false;
}

bool
FilterEvaluator::jsonEquals(const Json& lhs, const Json& rhs)
{
    if (lhs.getType() != rhs.getType()) {
        if (lhs.isNumber() && rhs.isNumber()) {
            double l, r;
            return toNumber(lhs, l) && toNumber(rhs, r) && l == r;
        }
        return false;
    }
    if (lhs.isNull())
        return true;
    if (lhs.isBool())
        return lhs.getBool() == rhs.getBool();
    if (lhs.isLong())
        return lhs.getLong() == rhs.getLong();
    if (lhs.isDouble() || lhs.isFloat())
        return lhs.getNumber() == rhs.getNumber();
    if (lhs.isString())
        return lhs.getString() == rhs.getString();
    if (lhs.isArray()) {
        const auto& larr = lhs.getArray();
        const auto& rarr = rhs.getArray();
        if (larr.size() != rarr.size())
            return false;
        for (size_t i = 0; i < larr.size(); ++i) {
            if (!jsonEquals(larr[i], rarr[i]))
                return false;
        }
        return true;
    }
    if (lhs.isObject()) {
        const auto& lobj = lhs.getObject();
        const auto& robj = rhs.getObject();
        if (lobj.size() != robj.size())
            return false;
        auto lit = lobj.begin();
        auto rit = robj.begin();
        for (; lit != lobj.end(); ++lit, ++rit) {
            if (lit->first != rit->first)
                return false;
            if (!jsonEquals(lit->second, rit->second))
                return false;
        }
        return true;
    }
    return false;
}

bool
FilterEvaluator::compareNumbers(double lhs, double rhs, const std::string& op)
{
    if (op == "<")
        return lhs < rhs;
    if (op == "<=")
        return lhs <= rhs;
    if (op == ">")
        return lhs > rhs;
    if (op == ">=")
        return lhs >= rhs;
    return false;
}

bool
FilterEvaluator::compareStrings(const std::string& lhs,
                                const std::string& rhs,
                                const std::string& op)
{
    if (op == "<")
        return lhs < rhs;
    if (op == "<=")
        return lhs <= rhs;
    if (op == ">")
        return lhs > rhs;
    if (op == ">=")
        return lhs >= rhs;
    return false;
}

long long
FilterEvaluator::computeLength(const Json& value)
{
    if (value.isString())
        return static_cast<long long>(value.getString().size());
    if (value.isArray())
        return static_cast<long long>(value.getArray().size());
    if (value.isObject())
        return static_cast<long long>(value.getObject().size());
    return 0;
}



template <typename JsonType>
static std::vector<JsonType*>
evaluatePathInternal(JsonType* start,
                     const std::vector<JsonPathStep>& steps,
                     JsonType* documentRoot)
{
    std::vector<JsonType*> current;
    current.reserve(1);
    current.push_back(start);
    if (steps.empty())
        return current;
    std::vector<JsonType*> next;
    next.reserve(4);
    std::vector<JsonType*> baseBuffer;
    baseBuffer.reserve(4);
    std::vector<JsonType*> recursionStack;
    recursionStack.reserve(16);

    for (const JsonPathStep& step : steps) {
        const std::vector<JsonType*>* base = &current;
        if (step.recursive) {
            baseBuffer.clear();
            if (!current.empty()) {
                baseBuffer.reserve(current.size() * 4);
                for (JsonType* node : current)
                    collectDescendants(node, baseBuffer, recursionStack);
            }
            base = &baseBuffer;
        }

        next.clear();
        if (!base->empty()) {
            size_t estimatedCapacity = base->size();
            switch (step.kind) {
                case JsonPathStep::Kind::Wildcard:
                    estimatedCapacity *= 8;
                    break;
                case JsonPathStep::Kind::Union:
                    estimatedCapacity *= step.unionEntries.size();
                    break;
                case JsonPathStep::Kind::Indices:
                    estimatedCapacity *= step.indices.size();
                    break;
                default:
                    break;
            }
            next.reserve(estimatedCapacity);
        }

        for (JsonType* node : *base) {
            switch (step.kind) {
                case JsonPathStep::Kind::Name: {
                    if (!node->isObject())
                        break;
                    auto& obj = JsonAccessor<JsonType>::getObject(*node);
                    auto it = obj.find(step.name);
                    if (it != obj.end())
                        next.push_back(&it->second);
                    break;
                }
                case JsonPathStep::Kind::Wildcard: {
                    if (node->isArray()) {
                        auto& arr = JsonAccessor<JsonType>::getArray(*node);
                        const size_t arrSize = arr.size();
                        if (arrSize > 0) {
                            next.reserve(next.size() + arrSize);
                            for (size_t i = 0; i < arrSize; ++i) {
                                if (i + kPrefetchDistance < arrSize)
                                    prefetch(&arr[i + kPrefetchDistance]);
                                next.push_back(&arr[i]);
                            }
                        }
                    } else if (node->isObject()) {
                        auto& obj = JsonAccessor<JsonType>::getObject(*node);
                        const size_t objSize = obj.size();
                        if (objSize > 0) {
                            next.reserve(next.size() + objSize);
                            for (auto it = obj.begin(); it != obj.end(); ++it)
                                next.push_back(&it->second);
                        }
                    }
                    break;
                }
                case JsonPathStep::Kind::Indices: {
                    if (!node->isArray())
                        break;
                    auto& arr = JsonAccessor<JsonType>::getArray(*node);
                    const size_t indicesCount = step.indices.size();
                    if (indicesCount > 0) {
                        next.reserve(next.size() + indicesCount);
                        for (long long raw : step.indices) {
                            size_t idx;
                            if (normalizeIndex(raw, arr.size(), idx))
                                next.push_back(&arr[idx]);
                        }
                    }
                    break;
                }
                case JsonPathStep::Kind::Slice:
                    applySlice(node, step.slice, next);
                    break;
                case JsonPathStep::Kind::Union: {
                    for (const auto& entry : step.unionEntries)
                        applyUnionEntry(node, entry, next);
                    break;
                }
                case JsonPathStep::Kind::Filter: {
                    if (!step.filter)
                        break;
                    const Json& docRef = static_cast<const Json&>(*documentRoot);
                    if (node->isArray()) {
                        auto& arr = JsonAccessor<JsonType>::getArray(*node);
                        const size_t arrSize = arr.size();
                        if (arrSize > 0) {
                            next.reserve(next.size() + arrSize / 2);
                            for (size_t i = 0; i < arrSize; ++i) {
                                if (i + kPrefetchDistance < arrSize)
                                    prefetch(&arr[i + kPrefetchDistance]);
                                JsonType* candidate = &arr[i];
                                if (FilterEvaluator::evaluate(step.filter,
                                                              docRef,
                                                              static_cast<const Json&>(arr[i])))
                                    next.push_back(candidate);
                            }
                        }
                    } else if (node->isObject()) {
                        auto& obj = JsonAccessor<JsonType>::getObject(*node);
                        const size_t objSize = obj.size();
                        if (objSize > 0) {
                            next.reserve(next.size() + objSize / 2);
                            for (auto it = obj.begin(); it != obj.end(); ++it) {
                                if (FilterEvaluator::evaluate(step.filter,
                                                              docRef,
                                                              static_cast<const Json&>(it->second)))
                                    next.push_back(&it->second);
                            }
                        }
                    }
                    break;
                }
            }
        }
        current.swap(next);
    }
    return current;
}

static std::vector<Json*>
evaluatePathMutable(Json& start,
                    const std::vector<JsonPathStep>& steps,
                    Json& documentRoot)
{
    return evaluatePathInternal<Json>(&start, steps, &documentRoot);
}

static std::vector<const Json*>
evaluatePathConst(const Json& start,
                  const std::vector<JsonPathStep>& steps,
                  const Json& documentRoot)
{
    return evaluatePathInternal<const Json>(&start, steps, &documentRoot);
}

} // namespace detail

std::vector<Json*>
Json::jsonpath(const std::string& expression)
{
    const detail::CompiledPath& compiled = detail::getCompiledPathCached(expression);
    if (compiled.relative)
        throw std::runtime_error("JSONPath expression must start with '$'");
    return detail::evaluatePathInternal<Json>(this, compiled.steps, this);
}

std::vector<const Json*>
Json::jsonpath(const std::string& expression) const
{
    const detail::CompiledPath& compiled = detail::getCompiledPathCached(expression);
    if (compiled.relative)
        throw std::runtime_error("JSONPath expression must start with '$'");
    return detail::evaluatePathInternal<const Json>(this, compiled.steps, this);
}

namespace detail {

struct JsonPathNodeWithParent
{
    Json* node;
    Json* parent;
    enum { ArrayIndex, ObjectKey, Root } locationType;
    size_t arrayIndex;
    std::string objectKey;
    
    JsonPathNodeWithParent(Json* n) 
        : node(n), parent(nullptr), locationType(Root), arrayIndex(0)
    {
    }
};

static void
collectDescendantsWithParent(JsonPathNodeWithParent item,
                              std::vector<JsonPathNodeWithParent>& out,
                              std::vector<JsonPathNodeWithParent>& stack)
{
    stack.clear();
    stack.push_back(item);
    bool skipRoot = true;
    while (!stack.empty()) {
        JsonPathNodeWithParent current = stack.back();
        stack.pop_back();
        if (skipRoot) {
            skipRoot = false;
        } else {
            out.push_back(current);
        }
        Json* node = current.node;
        if (node->isArray()) {
            auto& arr = node->getArray();
            const size_t arrSize = arr.size();
            if (arrSize > 0) {
                out.reserve(out.size() + arrSize);
                stack.reserve(stack.size() + arrSize);
                for (size_t i = arrSize; i-- > 0;) {
                    JsonPathNodeWithParent child(&arr[i]);
                    child.parent = node;
                    child.locationType = JsonPathNodeWithParent::ArrayIndex;
                    child.arrayIndex = i;
                    stack.push_back(child);
                }
            }
        } else if (node->isObject()) {
            auto& obj = node->getObject();
            const size_t objSize = obj.size();
            if (objSize > 0) {
                out.reserve(out.size() + objSize);
                stack.reserve(stack.size() + objSize);
                for (auto it = obj.rbegin(); it != obj.rend(); ++it) {
                    JsonPathNodeWithParent child(&it->second);
                    child.parent = node;
                    child.locationType = JsonPathNodeWithParent::ObjectKey;
                    child.objectKey = it->first;
                    stack.push_back(child);
                }
            }
        }
    }
}

static std::vector<JsonPathNodeWithParent>
evaluatePathWithParentInternal(Json* start,
                                const std::vector<JsonPathStep>& steps,
                                Json* documentRoot)
{
    std::vector<JsonPathNodeWithParent> current;
    current.reserve(1);
    current.emplace_back(start);
    if (steps.empty())
        return current;

    std::vector<JsonPathNodeWithParent> next;
    next.reserve(4);
    std::vector<JsonPathNodeWithParent> baseBuffer;
    baseBuffer.reserve(4);
    std::vector<JsonPathNodeWithParent> recursionStack;
    recursionStack.reserve(16);

    for (const JsonPathStep& step : steps) {
        const std::vector<JsonPathNodeWithParent>* base = &current;
        if (step.recursive) {
            baseBuffer.clear();
            if (!current.empty()) {
                baseBuffer.reserve(current.size() * 4);
                for (const auto& item : current)
                    collectDescendantsWithParent(item, baseBuffer, recursionStack);
            }
            base = &baseBuffer;
        }

        next.clear();
        if (!base->empty()) {
            size_t estimatedCapacity = base->size();
            switch (step.kind) {
                case JsonPathStep::Kind::Wildcard:
                    estimatedCapacity *= 8;
                    break;
                case JsonPathStep::Kind::Union:
                    estimatedCapacity *= step.unionEntries.size();
                    break;
                case JsonPathStep::Kind::Indices:
                    estimatedCapacity *= step.indices.size();
                    break;
                default:
                    break;
            }
            next.reserve(estimatedCapacity);
        }

        for (const auto& item : *base) {
            Json* node = item.node;
            switch (step.kind) {
                case JsonPathStep::Kind::Name: {
                    if (!node->isObject())
                        break;
                    auto& obj = node->getObject();
                    auto it = obj.find(step.name);
                    if (it != obj.end()) {
                        JsonPathNodeWithParent child(&it->second);
                        child.parent = node;
                        child.locationType = JsonPathNodeWithParent::ObjectKey;
                        child.objectKey = it->first;
                        next.push_back(child);
                    }
                    break;
                }
                case JsonPathStep::Kind::Wildcard: {
                    if (node->isArray()) {
                        auto& arr = node->getArray();
                        const size_t arrSize = arr.size();
                        if (arrSize > 0) {
                            next.reserve(next.size() + arrSize);
                            for (size_t i = 0; i < arrSize; ++i) {
                                if (i + kPrefetchDistance < arrSize)
                                    prefetch(&arr[i + kPrefetchDistance]);
                                JsonPathNodeWithParent child(&arr[i]);
                                child.parent = node;
                                child.locationType = JsonPathNodeWithParent::ArrayIndex;
                                child.arrayIndex = i;
                                next.push_back(child);
                            }
                        }
                    } else if (node->isObject()) {
                        auto& obj = node->getObject();
                        const size_t objSize = obj.size();
                        if (objSize > 0) {
                            next.reserve(next.size() + objSize);
                            for (auto it = obj.begin(); it != obj.end(); ++it) {
                                JsonPathNodeWithParent child(&it->second);
                                child.parent = node;
                                child.locationType = JsonPathNodeWithParent::ObjectKey;
                                child.objectKey = it->first;
                                next.push_back(child);
                            }
                        }
                    }
                    break;
                }
                case JsonPathStep::Kind::Indices: {
                    if (!node->isArray())
                        break;
                    auto& arr = node->getArray();
                    const size_t indicesCount = step.indices.size();
                    if (indicesCount > 0) {
                        next.reserve(next.size() + indicesCount);
                        for (long long raw : step.indices) {
                            size_t idx;
                            if (normalizeIndex(raw, arr.size(), idx)) {
                                JsonPathNodeWithParent child(&arr[idx]);
                                child.parent = node;
                                child.locationType = JsonPathNodeWithParent::ArrayIndex;
                                child.arrayIndex = idx;
                                next.push_back(child);
                            }
                        }
                    }
                    break;
                }
                case JsonPathStep::Kind::Slice: {
                    if (!node->isArray())
                        break;
                    auto& arr = node->getArray();
                    const long long arrSize = static_cast<long long>(arr.size());
                    if (arrSize == 0)
                        break;
                    const long long sliceStep = step.slice.hasStep ? step.slice.step : 1;
                    if (sliceStep == 0)
                        throw std::runtime_error("JSONPath slice step cannot be zero");
                    if (sliceStep > 0) {
                        long long start = step.slice.hasStart ? step.slice.start : 0;
                        long long end = step.slice.hasEnd ? step.slice.end : arrSize;
                        if (start < 0)
                            start += arrSize;
                        if (end < 0)
                            end += arrSize;
                        start = std::max(0LL, std::min(start, arrSize));
                        end = std::max(0LL, std::min(end, arrSize));
                        if (start < end) {
                            const size_t expectedCount = static_cast<size_t>((end - start + sliceStep - 1) / sliceStep);
                            next.reserve(next.size() + expectedCount);
                        }
                        for (long long i = start; i < end; i += sliceStep) {
                            JsonPathNodeWithParent child(&arr[static_cast<size_t>(i)]);
                            child.parent = node;
                            child.locationType = JsonPathNodeWithParent::ArrayIndex;
                            child.arrayIndex = static_cast<size_t>(i);
                            next.push_back(child);
                        }
                    } else {
                        long long start = step.slice.hasStart ? step.slice.start : (arrSize - 1);
                        long long end = step.slice.hasEnd ? step.slice.end : -1;
                        if (start < 0)
                            start += arrSize;
                        if (end < 0)
                            end += arrSize;
                        if (start >= arrSize)
                            start = arrSize - 1;
                        if (start < 0)
                            start = -1;
                        if (end >= arrSize)
                            end = arrSize - 1;
                        if (end < -1)
                            end = -1;
                        if (start > end) {
                            const size_t expectedCount = static_cast<size_t>((start - end - sliceStep - 1) / (-sliceStep));
                            next.reserve(next.size() + expectedCount);
                        }
                        for (long long i = start; i > end; i += sliceStep) {
                            if (i >= 0 && i < arrSize) {
                                JsonPathNodeWithParent child(&arr[static_cast<size_t>(i)]);
                                child.parent = node;
                                child.locationType = JsonPathNodeWithParent::ArrayIndex;
                                child.arrayIndex = static_cast<size_t>(i);
                                next.push_back(child);
                            }
                        }
                    }
                    break;
                }
                case JsonPathStep::Kind::Union: {
                    for (const auto& entry : step.unionEntries) {
                        switch (entry.kind) {
                            case JsonPathUnionKind::Name: {
                                if (!node->isObject())
                                    break;
                                auto& obj = node->getObject();
                                auto it = obj.find(entry.name);
                                if (it != obj.end()) {
                                    JsonPathNodeWithParent child(&it->second);
                                    child.parent = node;
                                    child.locationType = JsonPathNodeWithParent::ObjectKey;
                                    child.objectKey = it->first;
                                    next.push_back(child);
                                }
                                break;
                            }
                            case JsonPathUnionKind::Index: {
                                if (!node->isArray())
                                    break;
                                auto& arr = node->getArray();
                                size_t idx;
                                if (normalizeIndex(entry.index, arr.size(), idx)) {
                                    JsonPathNodeWithParent child(&arr[idx]);
                                    child.parent = node;
                                    child.locationType = JsonPathNodeWithParent::ArrayIndex;
                                    child.arrayIndex = idx;
                                    next.push_back(child);
                                }
                                break;
                            }
                            case JsonPathUnionKind::Slice:
                                break;
                            case JsonPathUnionKind::Wildcard:
                                if (node->isArray()) {
                                    auto& arr = node->getArray();
                                    const size_t arrSize = arr.size();
                                    if (arrSize > 0) {
                                        next.reserve(next.size() + arrSize);
                                        for (size_t i = 0; i < arrSize; ++i) {
                                            JsonPathNodeWithParent child(&arr[i]);
                                            child.parent = node;
                                            child.locationType = JsonPathNodeWithParent::ArrayIndex;
                                            child.arrayIndex = i;
                                            next.push_back(child);
                                        }
                                    }
                                } else if (node->isObject()) {
                                    auto& obj = node->getObject();
                                    const size_t objSize = obj.size();
                                    if (objSize > 0) {
                                        next.reserve(next.size() + objSize);
                                        for (auto it = obj.begin(); it != obj.end(); ++it) {
                                            JsonPathNodeWithParent child(&it->second);
                                            child.parent = node;
                                            child.locationType = JsonPathNodeWithParent::ObjectKey;
                                            child.objectKey = it->first;
                                            next.push_back(child);
                                        }
                                    }
                                }
                                break;
                        }
                    }
                    break;
                }
                case JsonPathStep::Kind::Filter: {
                    if (!step.filter)
                        break;
                    const Json& docRef = static_cast<const Json&>(*documentRoot);
                    if (node->isArray()) {
                        auto& arr = node->getArray();
                        const size_t arrSize = arr.size();
                        if (arrSize > 0) {
                            next.reserve(next.size() + arrSize / 2);
                            for (size_t i = 0; i < arrSize; ++i) {
                                if (i + kPrefetchDistance < arrSize)
                                    prefetch(&arr[i + kPrefetchDistance]);
                                if (FilterEvaluator::evaluate(step.filter,
                                                              docRef,
                                                              static_cast<const Json&>(arr[i]))) {
                                    JsonPathNodeWithParent child(&arr[i]);
                                    child.parent = node;
                                    child.locationType = JsonPathNodeWithParent::ArrayIndex;
                                    child.arrayIndex = i;
                                    next.push_back(child);
                                }
                            }
                        }
                    } else if (node->isObject()) {
                        auto& obj = node->getObject();
                        const size_t objSize = obj.size();
                        if (objSize > 0) {
                            next.reserve(next.size() + objSize / 2);
                            for (auto it = obj.begin(); it != obj.end(); ++it) {
                                if (FilterEvaluator::evaluate(step.filter,
                                                              docRef,
                                                              static_cast<const Json&>(it->second))) {
                                    JsonPathNodeWithParent child(&it->second);
                                    child.parent = node;
                                    child.locationType = JsonPathNodeWithParent::ObjectKey;
                                    child.objectKey = it->first;
                                    next.push_back(child);
                                }
                            }
                        }
                    }
                    break;
                }
            }
        }
        current.swap(next);
    }
    return current;
}

} // namespace detail

size_t
Json::updateJsonpath(const std::string& expression, const Json& value)
{
    auto matches = jsonpath(expression);
    size_t count = 0;
    for (Json* node : matches) {
        *node = value;
        ++count;
    }
    return count;
}

size_t
Json::updateJsonpath(const std::string& expression, Json&& value)
{
    auto matches = jsonpath(expression);
    size_t count = 0;
    if (matches.empty())
        return 0;
    
    // For move assignment, we need to create a copy for each match except the first
    Json firstValue = std::move(value);
    *matches[0] = std::move(firstValue);
    ++count;
    
    // Copy for remaining matches
    for (size_t i = 1; i < matches.size(); ++i) {
        *matches[i] = *matches[0];
        ++count;
    }
    return count;
}

size_t
Json::deleteJsonpath(const std::string& expression)
{
    const detail::CompiledPath& compiled = detail::getCompiledPathCached(expression);
    if (compiled.relative)
        throw std::runtime_error("JSONPath expression must start with '$'");
    
    auto matches = detail::evaluatePathWithParentInternal(this, compiled.steps, this);
    
    // Sort by reverse order to avoid index shifting issues when deleting from arrays
    // Sort by array index descending, object keys can be in any order
    std::sort(matches.begin(), matches.end(), [](const detail::JsonPathNodeWithParent& a, const detail::JsonPathNodeWithParent& b) {
        if (a.locationType == detail::JsonPathNodeWithParent::ArrayIndex &&
            b.locationType == detail::JsonPathNodeWithParent::ArrayIndex) {
            return a.arrayIndex > b.arrayIndex; // Descending order
        }
        return false; // Keep original order for others
    });
    
    size_t count = 0;
    for (const auto& match : matches) {
        if (match.parent == nullptr) {
            // Can't delete root
            continue;
        }
        
        if (match.locationType == detail::JsonPathNodeWithParent::ArrayIndex) {
            if (match.parent->isArray()) {
                auto& arr = match.parent->getArray();
                if (match.arrayIndex < arr.size()) {
                    arr.erase(arr.begin() + match.arrayIndex);
                    ++count;
                }
            }
        } else if (match.locationType == detail::JsonPathNodeWithParent::ObjectKey) {
            if (match.parent->isObject()) {
                auto& obj = match.parent->getObject();
                auto it = obj.find(match.objectKey);
                if (it != obj.end()) {
                    obj.erase(it);
                    ++count;
                }
            }
        }
    }
    return count;
}


const char*
Json::StatusToString(Json::Status status)
{
    switch (status) {
        case success:
            return "success";
        case bad_double:
            return "bad_double";
        case absent_value:
            return "absent_value";
        case bad_negative:
            return "bad_negative";
        case bad_exponent:
            return "bad_exponent";
        case missing_comma:
            return "missing_comma";
        case missing_colon:
            return "missing_colon";
        case malformed_utf8:
            return "malformed_utf8";
        case depth_exceeded:
            return "depth_exceeded";
        case stack_overflow:
            return "stack_overflow";
        case unexpected_eof:
            return "unexpected_eof";
        case overlong_ascii:
            return "overlong_ascii";
        case unexpected_comma:
            return "unexpected_comma";
        case unexpected_colon:
            return "unexpected_colon";
        case unexpected_octal:
            return "unexpected_octal";
        case trailing_content:
            return "trailing_content";
        case illegal_character:
            return "illegal_character";
        case invalid_hex_escape:
            return "invalid_hex_escape";
        case overlong_utf8_0x7ff:
            return "overlong_utf8_0x7ff";
        case overlong_utf8_0xffff:
            return "overlong_utf8_0xffff";
        case object_missing_value:
            return "object_missing_value";
        case illegal_utf8_character:
            return "illegal_utf8_character";
        case invalid_unicode_escape:
            return "invalid_unicode_escape";
        case utf16_surrogate_in_utf8:
            return "utf16_surrogate_in_utf8";
        case unexpected_end_of_array:
            return "unexpected_end_of_array";
        case hex_escape_not_printable:
            return "hex_escape_not_printable";
        case invalid_escape_character:
            return "invalid_escape_character";
        case utf8_exceeds_utf16_range:
            return "utf8_exceeds_utf16_range";
        case unexpected_end_of_string:
            return "unexpected_end_of_string";
        case unexpected_end_of_object:
            return "unexpected_end_of_object";
        case object_key_must_be_string:
            return "object_key_must_be_string";
        case c1_control_code_in_string:
            return "c1_control_code_in_string";
        case non_del_c0_control_code_in_string:
            return "non_del_c0_control_code_in_string";
        default:
            ON_LOGIC_ERROR("Unhandled Json status value.");
    }
}

} // namespace jt
