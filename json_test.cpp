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
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>

#define ARRAYLEN(A) \
    ((sizeof(A) / sizeof(*(A))) / ((unsigned)!(sizeof(A) % sizeof(*(A)))))

#define STRING(sl) std::string(sl, sizeof(sl) - 1)

using jt::Json;

static const char kHuge[] = R"([
    "JSON Test Pattern pass1",
    {"object with 1 member":["array with 1 element"]},
    {},
    [],
    -42,
    true,
    false,
    null,
    {
        "integer": 1234567890,
        "real": -9876.543210,
        "e": 0.123456789e-12,
        "E": 1.234567890E+34,
        "":  23456789012E66,
        "zero": 0,
        "one": 1,
        "space": " ",
        "quote": "\"",
        "backslash": "\\",
        "controls": "\b\f\n\r\t",
        "slash": "/ & \/",
        "alpha": "abcdefghijklmnopqrstuvwyz",
        "ALPHA": "ABCDEFGHIJKLMNOPQRSTUVWYZ",
        "digit": "0123456789",
        "0123456789": "digit",
        "special": "`1~!@#$%^&*()_+-={':[,]}|;.</>?",
        "hex": "\u0123\u4567\u89AB\uCDEF\uabcd\uef4A",
        "true": true,
        "false": false,
        "null": null,
        "array":[  ],
        "object":{  },
        "address": "50 St. James Street",
        "url": "http://www.JSON.org/",
        "comment": "// /* <!-- --",
        "# -- --> */": " ",
        " s p a c e d " :[1,2 , 3

,

4 , 5        ,          6           ,7        ],"compact":[1,2,3,4,5,6,7],
        "jsontext": "{\"object with 1 member\":[\"array with 1 element\"]}",
        "quotes": "&#34; \u0022 %22 0x22 034 &#x22;",
        "\/\\\"\uCAFE\uBABE\uAB98\uFCDE\ubcda\uef4A\b\f\n\r\t`1~!@#$%^&*()_+-=[]{}|;:',./<>?"
: "A key can be any string"
    },
    0.5 ,98.6
,
99.44
,

1066,
1e1,
0.1e1,
1e-1,
1e00,2e+00,2e-00
,"rosebud"])";

static const char kStoreExample[] = R"({
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

#define BENCH(ITERATIONS, WORK_PER_RUN, CODE) \
    do { \
        auto start = std::chrono::high_resolution_clock::now(); \
        for (int __i = 0; __i < ITERATIONS; ++__i) { \
            std::atomic_signal_fence(std::memory_order_acq_rel); \
            CODE; \
        } \
        auto end = std::chrono::high_resolution_clock::now(); \
        auto duration = \
          std::chrono::duration_cast<std::chrono::nanoseconds>(end - start); \
        long long work = (WORK_PER_RUN) * (ITERATIONS); \
        double nanos = (duration.count() + work - 1) / (double)work; \
        printf("%10g ns %2dx %s\n", nanos, (ITERATIONS), #CODE); \
    } while (0)

void
object_test()
{
    Json obj;
    obj["content"] = "hello";
    if (obj.toString() != "{\"content\":\"hello\"}")
        exit(1);
}

void
deep_test()
{
    Json A1;
    A1[0] = 0;
    A1[1] = 10;
    A1[2] = 20;
    A1[3] = 3.14;
    A1[4] = 40;
    Json A2;
    A2[0] = std::move(A1);
    Json A3;
    A3[0] = std::move(A2);
    Json obj;
    obj["content"] = std::move(A3);
    if (obj.toString() != "{\"content\":[[[0,10,20,3.14,40]]]}")
        exit(2);
}

void
parse_test()
{
    std::pair<Json::Status, Json> res =
      Json::parse("{ \"content\":[[[0,10,20,3.14,40]]]}");
    if (res.first != Json::success)
        exit(3);
    if (res.second.toString() != "{\"content\":[[[0,10,20,3.14,40]]]}")
        exit(4);
    if (res.second.toStringPretty() !=
        R"({"content": [[[0, 10, 20, 3.14, 40]]]})")
        exit(5);
    res = Json::parse("{ \"a\": 1, \"b\": [2,   3]}");
    if (res.second.toString() != R"({"a":1,"b":[2,3]})")
        exit(6);
    if (res.second.toStringPretty() !=
        R"({
  "a": 1,
  "b": [2, 3]
})")
        exit(7);
}


void
jsonpath_test()
{
    auto parsed = Json::parse(kStoreExample);
    if (parsed.first != Json::success)
        exit(90);
    Json& json = parsed.second;

    auto authors = json.jsonpath("$.store.book[*].author");
    if (authors.size() != 4)
        exit(91);
    if (!authors[0]->isString() || authors[0]->getString() != "Nigel Rees")
        exit(92);

    auto cheap = json.jsonpath("$.store.book[?(@.price < 10)].title");
    if (cheap.size() != 2)
        exit(93);
    if (cheap[0]->getString() != "Sayings of the Century" ||
        cheap[1]->getString() != "Moby Dick")
        exit(94);

    auto recursive = json.jsonpath("$..price");
    if (recursive.size() != 5)
        exit(95);

    auto slice = json.jsonpath("$.store.book[1:3].author");
    if (slice.size() != 2 || slice[0]->getString() != "Evelyn Waugh" ||
        slice[1]->getString() != "Herman Melville")
        exit(96);

    auto unionNodes = json.jsonpath("$.store['bicycle','book']");
    if (unionNodes.size() != 2 || !unionNodes[0]->isObject() ||
        !unionNodes[1]->isArray())
        exit(97);

    const Json& cref = json;
    auto constAuthors = cref.jsonpath("$..author");
    if (constAuthors.size() != 4)
        exit(98);
}

void
jsonpath_update_delete_test()
{
    // Test update
    auto parsed = Json::parse(kStoreExample);
    if (parsed.first != Json::success)
        exit(100);
    Json json = parsed.second;

    // Update single field
    size_t count = json.updateJsonpath("$.expensive", Json(20));
    if (count != 1)
        exit(101);
    if (json["expensive"].getLong() != 20)
        exit(102);

    // Update multiple fields
    count = json.updateJsonpath("$.store.book[*].price", Json(9.99));
    if (count != 4)
        exit(103);
    auto prices = json.jsonpath("$.store.book[*].price");
    for (const Json* price : prices) {
        if (price->getDouble() != 9.99)
            exit(104);
    }

    // Test delete
    Json testObj = Json::parse(R"({"a": 1, "b": 2, "c": 3})").second;
    count = testObj.deleteJsonpath("$.b");
    if (count != 1)
        exit(105);
    if (testObj.toString() != R"({"a":1,"c":3})")
        exit(106);

    // Delete from array
    Json testArr = Json::parse(R"([1, 2, 3, 4, 5])").second;
    count = testArr.deleteJsonpath("$[1:3]");
    if (count != 2)
        exit(107);
    if (testArr.toString() != "[1,4,5]")
        exit(108);

    // Delete multiple matching fields
    Json testMulti = Json::parse(R"({"items": [{"id": 1, "name": "a"}, {"id": 2, "name": "b"}, {"id": 3, "name": "c"}]})").second;
    count = testMulti.deleteJsonpath("$.items[*].name");
    if (count != 3)
        exit(109);
}

// Performance test data - larger JSON structure for realistic benchmarks
static const char kLargeJsonExample[] = R"({
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

void
jsonpath_query_perf_test()
{
    auto parsed = Json::parse(kLargeJsonExample);
    if (parsed.first != Json::success)
        exit(200);
    Json& json = parsed.second;

    // Test simple path query
    auto authors = json.jsonpath("$.store.book[*].author");
    if (authors.size() != 8)
        exit(201);

    // Test recursive query
    auto prices = json.jsonpath("$..price");
    if (prices.size() != 13)  // 8 books + 1 bicycle + 1 car + 3 electronics
        exit(202);

    // Test filter query
    auto cheap = json.jsonpath("$.store.book[?(@.price < 10)].title");
    if (cheap.size() != 4)  // 8.95, 8.99, 9.95, 7.99
        exit(203);

    // Test slice query
    auto slice = json.jsonpath("$.store.book[1:5].author");
    if (slice.size() != 4)
        exit(204);

    // Test union query
    auto unionNodes = json.jsonpath("$.store['bicycle','car']");
    if (unionNodes.size() != 2)
        exit(205);

    // Test const version
    const Json& cref = json;
    auto constPrices = cref.jsonpath("$..price");
    if (constPrices.size() != 13)
        exit(206);
}

void
jsonpath_update_perf_test()
{
    auto parsed = Json::parse(kLargeJsonExample);
    if (parsed.first != Json::success)
        exit(210);
    Json json = parsed.second;

    // Update single field
    size_t count = json.updateJsonpath("$.expensive", Json(20));
    if (count != 1)
        exit(211);

    // Update multiple fields
    count = json.updateJsonpath("$.store.book[*].price", Json(9.99));
    if (count != 8)
        exit(212);

    // Update with filter
    count = json.updateJsonpath("$.store.electronics[?(@.stock > 20)].stock", Json(30));
    if (count != 1)
        exit(213);
}

void
jsonpath_delete_perf_test()
{
    // Test delete single field
    Json testObj = Json::parse(R"({"a": 1, "b": 2, "c": 3, "d": 4})").second;
    size_t count = testObj.deleteJsonpath("$.b");
    if (count != 1)
        exit(220);

    // Test delete from array
    Json testArr = Json::parse(R"([1, 2, 3, 4, 5, 6, 7, 8])").second;
    count = testArr.deleteJsonpath("$[1:4]");
    if (count != 3)
        exit(221);

    // Test delete multiple matching fields
    Json testMulti = Json::parse(kLargeJsonExample).second;
    count = testMulti.deleteJsonpath("$.store.book[*].price");
    if (count != 8)
        exit(222);
}

void
jsonpath_complex_perf_test()
{
    auto parsed = Json::parse(kLargeJsonExample);
    if (parsed.first != Json::success)
        exit(230);
    Json& json = parsed.second;

    // Complex nested query
    auto nested = json.jsonpath("$.store.book[?(@.category == 'fiction' && @.price < 15)].author");
    if (nested.size() != 5)  // 12.99, 8.99, 9.95, 11.50, 7.99
        exit(231);

    // Deep recursive query
    auto deep = json.jsonpath("$..*");
    if (deep.size() == 0)
        exit(232);

    // Multiple filters
    auto filtered = json.jsonpath("$.store.book[?(@.price > 10 && @.price < 20)].title");
    if (filtered.size() != 3)  // 12.99, 11.50, 15.00
        exit(233);
}

static const struct
{
    std::string before;
    std::string after;
} kRoundTrip[] = {

    // types
    { "0", "0" },
    { "[]", "[]" },
    { "{}", "{}" },
    { "0.1", "0.1" },
    { "\"\"", "\"\"" },
    { "null", "null" },
    { "true", "true" },
    { "false", "false" },

    // valid utf16 sequences
    { " [\"\\u0020\"] ", "[\" \"]" },
    { " [\"\\u00A0\"] ", "[\"\\u00a0\"]" },

    // when we encounter invalid utf16 sequences
    // we turn them into ascii
    { "[\"\\uDFAA\"]", "[\"\\\\uDFAA\"]" },
    { " [\"\\uDd1e\\uD834\"] ", "[\"\\\\uDd1e\\\\uD834\"]" },
    { " [\"\\ud800abc\"] ", "[\"\\\\ud800abc\"]" },
    { " [\"\\ud800\"] ", "[\"\\\\ud800\"]" },
    { " [\"\\uD800\\uD800\\n\"] ", "[\"\\\\uD800\\\\uD800\\n\"]" },
    { " [\"\\uDd1ea\"] ", "[\"\\\\uDd1ea\"]" },
    { " [\"\\uD800\\n\"] ", "[\"\\\\uD800\\n\"]" },

    // underflow and overflow
    { " [123.456e-789] ", "[0]" },
    { " [0."
      "4e0066999999999999999999999999999999999999999999999999999999999999999999"
      "9999999999999999999999999999999999999999999999999969999999006] ",
      "[1e5000]" },
    { " [1.5e+9999] ", "[1e5000]" },
    { " [-1.5e+9999] ", "[-1e5000]" },
    { " [-123123123123123123123123123123] ", "[-1.2312312312312312e+29]" },
};

// https://github.com/nst/JSONTestSuite/
static const struct
{
    Json::Status error;
    std::string json;
} kJsonTestSuite[] = {
    { Json::absent_value, "" },
    { Json::trailing_content, "[] []" },
    { Json::illegal_character, "[nan]" },
    { Json::bad_negative, "[-nan]" },
    { Json::illegal_character, "[+NaN]" },
    { Json::trailing_content,
      "{\"Extra value after close\": true} \"misplaced quoted value\"" },
    { Json::illegal_character, "{\"Illegal expression\": 1 + 2}" },
    { Json::illegal_character, "{\"Illegal invocation\": alert()}" },
    { Json::unexpected_octal, "{\"Numbers cannot have leading zeroes\": 013}" },
    { Json::illegal_character, "{\"Numbers cannot be hex\": 0x14}" },
    { Json::hex_escape_not_printable, "[\"Illegal backslash escape: \\x15\"]" },
    { Json::illegal_character, "[\\naked]" },
    { Json::invalid_escape_character, "[\"Illegal backslash escape: \\017\"]" },
    { Json::depth_exceeded,
      "[[[[[[[[[[[[[[[[[[[[\"Too deep\"]]]]]]]]]]]]]]]]]]]]" },
    { Json::missing_colon, "{\"Missing colon\" null}" },
    { Json::unexpected_colon, "{\"Double colon\":: null}" },
    { Json::unexpected_comma, "{\"Comma instead of colon\", null}" },
    { Json::unexpected_colon, "[\"Colon instead of comma\": false]" },
    { Json::illegal_character, "[\"Bad value\", truth]" },
    { Json::illegal_character, "[\'single quote\']" },
    { Json::non_del_c0_control_code_in_string,
      "[\"\ttab\tcharacter\tin\tstring\t\"]" },
    { Json::invalid_escape_character,
      "[\"tab\\   character\\   in\\  string\\  \"]" },
    { Json::non_del_c0_control_code_in_string, "[\"line\nbreak\"]" },
    { Json::invalid_escape_character, "[\"line\\\nbreak\"]" },
    { Json::bad_exponent, "[0e]" },
    { Json::unexpected_eof, "[\"Unclosed array\"" },
    { Json::bad_exponent, "[0e+]" },
    { Json::bad_exponent, "[0e+-1]" },
    { Json::unexpected_eof, "{\"Comma instead if closing brace\": true," },
    { Json::unexpected_end_of_object, "[\"mismatch\"}" },
    { Json::illegal_character, "{unquoted_key: \"keys must be quoted\"}" },
    { Json::unexpected_end_of_array, "[\"extra comma\",]" },
    { Json::unexpected_comma, "[\"double extra comma\",,]" },
    { Json::unexpected_comma, "[   , \"<-- missing value\"]" },
    { Json::trailing_content, "[\"Comma after the close\"]," },
    { Json::trailing_content, "[\"Extra close\"]]" },
    { Json::unexpected_end_of_object, "{\"Extra comma\": true,}" },
    { Json::unexpected_eof, " {\"a\" " },
    { Json::unexpected_eof, " {\"a\": " },
    { Json::unexpected_colon, " {:\"b\" " },
    { Json::illegal_character, " {\"a\" b} " },
    { Json::illegal_character, " {key: 'value'} " },
    { Json::object_key_must_be_string, " {\"a\":\"a\" 123} " },
    { Json::illegal_character, " \x7b\xf0\x9f\x87\xa8\xf0\x9f\x87\xad\x7d " },
    { Json::object_key_must_be_string, " {[: \"x\"} " },
    { Json::illegal_character, " [1.8011670033376514H-308] " },
    { Json::illegal_character, " [1.2a-3] " },
    { Json::illegal_character, " [.123] " },
    { Json::bad_exponent, " [1e\xe5] " },
    { Json::bad_exponent, " [1ea] " },
    { Json::illegal_character, " [-1x] " },
    { Json::bad_negative, " [-.123] " },
    { Json::bad_negative, " [-foo] " },
    { Json::bad_negative, " [-Infinity] " },
    { Json::illegal_character, " \x5b\x30\xe5\x5d " },
    { Json::illegal_character, " \x5b\x31\x65\x31\xe5\x5d " },
    { Json::illegal_character, " \x5b\x31\x32\x33\xe5\x5d " },
    { Json::missing_comma,
      " \x5b\x2d\x31\x32\x33\x2e\x31\x32\x33\x66\x6f\x6f\x5d " },
    { Json::bad_exponent, " [0e+-1] " },
    { Json::illegal_character, " [Infinity] " },
    { Json::illegal_character, " [0x42] " },
    { Json::illegal_character, " [0x1] " },
    { Json::illegal_character, " [1+2] " },
    { Json::illegal_character, " \x5b\xef\xbc\x91\x5d " },
    { Json::illegal_character, " [NaN] " },
    { Json::illegal_character, " [Inf] " },
    { Json::bad_double, " [9.e+] " },
    { Json::bad_exponent, " [1eE2] " },
    { Json::bad_exponent, " [1e0e] " },
    { Json::bad_exponent, " [1.0e-] " },
    { Json::bad_exponent, " [1.0e+] " },
    { Json::bad_exponent, " [0e] " },
    { Json::bad_exponent, " [0e+] " },
    { Json::bad_exponent, " [0E] " },
    { Json::bad_exponent, " [0E+] " },
    { Json::bad_exponent, " [0.3e] " },
    { Json::bad_exponent, " [0.3e+] " },
    { Json::illegal_character, " [0.1.2] " },
    { Json::illegal_character, " [.2e-3] " },
    { Json::illegal_character, " [.-1] " },
    { Json::bad_negative, " [-NaN] " },
    { Json::illegal_character, " [+Inf] " },
    { Json::illegal_character, " [+1] " },
    { Json::illegal_character, " [++1234] " },
    { Json::illegal_character, " [tru] " },
    { Json::illegal_character, " [nul] " },
    { Json::illegal_character, " [fals] " },
    { Json::unexpected_eof, " [{} " },
    { Json::unexpected_eof, "\n[1,\n1\n,1  " },
    { Json::unexpected_eof, " [1, " },
    { Json::unexpected_eof, " [\"\" " },
    { Json::illegal_character, " [* " },
    { Json::non_del_c0_control_code_in_string,
      " \x5b\x22\x0b\x61\x22\x5c\x66\x5d " },
    { Json::unexpected_eof, "[\"a\",\n4\n,1,1  " },
    { Json::unexpected_colon, " [1:2] " },
    { Json::illegal_character, " \x5b\xff\x5d " },
    { Json::illegal_character, " \x5b\x78 " },
    { Json::unexpected_eof, " [\"x\" " },
    { Json::unexpected_colon, " [\"\": 1] " },
    { Json::illegal_character, " [a\xe5] " },
    { Json::unexpected_comma, " {\"x\", null} " },
    { Json::illegal_character, " [\"x\", truth] " },
    { Json::illegal_character, STRING("\x00") },
    { Json::trailing_content, "\n[\"x\"]]" },
    { Json::unexpected_octal, " [012] " },
    { Json::unexpected_octal, " [-012] " },
    { Json::missing_comma, " [1 000.0] " },
    { Json::unexpected_octal, " [-01] " },
    { Json::bad_negative, " [- 1] " },
    { Json::bad_negative, " [-] " },
    { Json::illegal_utf8_character, " {\"\xb9\":\"0\",} " },
    { Json::unexpected_colon, " {\"x\"::\"b\"} " },
    { Json::unexpected_comma, " [1,,] " },
    { Json::unexpected_end_of_array, " [1,] " },
    { Json::unexpected_comma, " [1,,2] " },
    { Json::unexpected_comma, " [,1] " },
    { Json::missing_comma, " [ 3[ 4]] " },
    { Json::missing_comma, " [1 true] " },
    { Json::missing_comma, " [\"a\" \"b\"] " },
    { Json::bad_negative, " [--2.] " },
    { Json::bad_double, " [1.] " },
    { Json::bad_double, " [2.e3] " },
    { Json::bad_double, " [2.e-3] " },
    { Json::bad_double, " [2.e+3] " },
    { Json::bad_double, " [0.e1] " },
    { Json::bad_double, " [-2.] " },
    { Json::illegal_character, " \xef\xbb\xbf{} " },
    { Json::illegal_character, STRING(" [\x00\"\x00\xe9\x00\"\x00]\x00 ") },
    { Json::illegal_character, STRING(" \x00[\x00\"\x00\xe9\x00\"\x00] ") },
    { Json::malformed_utf8, " [\"\xe0\xff\"] " },
    { Json::illegal_utf8_character, " [\"\xfc\x80\x80\x80\x80\x80\"] " },
    { Json::illegal_utf8_character, " [\"\xfc\x83\xbf\xbf\xbf\xbf\"] " },
    { Json::overlong_ascii, " [\"\xc0\xaf\"] " },
    { Json::utf8_exceeds_utf16_range, " [\"\xf4\xbf\xbf\xbf\"] " },
    { Json::c1_control_code_in_string, " [\"\x81\"] " },
    { Json::malformed_utf8, " [\"\xe9\"] " },
    { Json::illegal_utf8_character, " [\"\xff\"] " },
    { Json::success, kHuge },
    { Json::success,
      R"([[[[[[[[[[[[[[[[[[["Not too deep"]]]]]]]]]]]]]]]]]]])" },
    { Json::success, R"({
    "JSON Test Pattern pass3": {
        "The outermost value": "must be an object or array.",
        "In this test": "It is an object."
    }
}
)" },
};

void
round_trip_test()
{
    for (size_t i = 0; i < ARRAYLEN(kRoundTrip); ++i) {
        std::pair<Json::Status, Json> res = Json::parse(kRoundTrip[i].before);
        if (res.first != Json::success) {
            printf(
              "error: Json::parse returned Json::%s but wanted Json::%s: %s\n",
              Json::StatusToString(res.first),
              Json::StatusToString(Json::success),
              kRoundTrip[i].before.c_str());
            exit(10);
        }
        if (res.second.toString() != kRoundTrip[i].after) {
            printf("error: Json::parse(%s).toString() was %s but should have "
                   "been %s\n",
                   kRoundTrip[i].before.c_str(),
                   res.second.toString().c_str(),
                   kRoundTrip[i].after.c_str());
            exit(11);
        }
    }
}

void
json_test_suite()
{
    for (size_t i = 0; i < ARRAYLEN(kJsonTestSuite); ++i) {
        std::pair<Json::Status, Json> res = Json::parse(kJsonTestSuite[i].json);
        if (res.first != kJsonTestSuite[i].error) {
            printf(
              "error: Json::parse returned Json::%s but wanted Json::%s: %s\n",
              Json::StatusToString(res.first),
              Json::StatusToString(kJsonTestSuite[i].error),
              kJsonTestSuite[i].json.c_str());
            exit(12);
        }
    }
}

void
afl_regression()
{
    Json::parse("[{\"\":1,3:14,]\n");
    Json::parse("[\n"
                "\n"
                "3E14,\n"
                "{\"!\":4,733:4,[\n"
                "\n"
                "3EL%,3E14,\n"
                "{][1][1,,]");
    Json::parse("[\n"
                "null,\n"
                "1,\n"
                "3.14,\n"
                "{\"a\": \"b\",\n"
                "3:14,ull}\n"
                "]");
    Json::parse("[\n"
                "\n"
                "3E14,\n"
                "{\"a!!!!!!!!!!!!!!!!!!\":4, \n"
                "\n"
                "3:1,,\n"
                "3[\n"
                "\n"
                "]");
    Json::parse("[\n"
                "\n"
                "3E14,\n"
                "{\"a!!:!!!!!!!!!!!!!!!\":4, \n"
                "\n"
                "3E1:4, \n"
                "\n"
                "3E1,,\n"
                ",,\n"
                "3[\n"
                "\n"
                "]");
    Json::parse("[\n"
                "\n"
                "3E14,\n"
                "{\"!\":4,733:4,[\n"
                "\n"
                "3E1%,][1,,]");
    Json::parse("[\n"
                "\n"
                "3E14,\n"
                "{\"!\":4,733:4,[\n"
                "\n"
                "3EL%,3E14,\n"
                "{][1][1,,]");
}

int
main()
{
    object_test();
    deep_test();
    parse_test();
    jsonpath_test();
    jsonpath_update_delete_test();
    round_trip_test();
    afl_regression();
    json_test_suite();

    BENCH(2000, 1, object_test());
    BENCH(2000, 1, deep_test());
    BENCH(2000, 1, parse_test());
    BENCH(2000, 1, round_trip_test());
    BENCH(2000, 1, json_test_suite());
    
    // JSONPath performance tests
    BENCH(2000, 1, jsonpath_query_perf_test());
    BENCH(2000, 1, jsonpath_update_perf_test());
    BENCH(2000, 1, jsonpath_delete_perf_test());
    BENCH(2000, 1, jsonpath_complex_perf_test());
}
