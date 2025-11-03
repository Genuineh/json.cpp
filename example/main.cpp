// -*- mode:c++;indent-tabs-mode:nil;c-basic-offset:4;coding:utf-8 -*-
// vi: set et ft=cpp ts=4 sts=4 sw=4 fenc=utf-8 :vi
//
// Example usage of the jt::Json library
//
// This example demonstrates basic usage of the JSON library:
// - Parsing JSON strings
// - Creating JSON objects and arrays programmatically
// - Accessing and modifying JSON values
// - Serializing JSON to strings
// - Error handling

#include "../json.h"
#include <iostream>
#include <string>

using jt::Json;

// Example 1: Parse a JSON string
void example_parse()
{
    std::cout << "\n=== Example 1: Parsing JSON ===" << std::endl;
    
    const std::string json_str = R"({
        "name": "John Doe",
        "age": 30,
        "isActive": true,
        "balance": 1234.56
    })";
    
    auto result = Json::parse(json_str);
    Json::Status status = result.first;
    Json json = result.second;
    
    if (status != Json::success) {
        std::cerr << "Parse error: " << Json::StatusToString(status) << std::endl;
        return;
    }
    
    std::cout << "Parsed JSON:" << std::endl;
    std::cout << json.toStringPretty() << std::endl;
}

// Example 2: Create JSON object programmatically
void example_create_object()
{
    std::cout << "\n=== Example 2: Creating JSON Object ===" << std::endl;
    
    Json obj;
    obj["name"] = "Jane Smith";
    obj["age"] = 25;
    obj["email"] = "jane@example.com";
    obj["isActive"] = true;
    obj["score"] = 95.5;
    obj["tags"] = nullptr;  // null value
    
    std::cout << "Created JSON object:" << std::endl;
    std::cout << obj.toStringPretty() << std::endl;
}

// Example 3: Create JSON array
void example_create_array()
{
    std::cout << "\n=== Example 3: Creating JSON Array ===" << std::endl;
    
    Json arr;
    arr.setArray();
    arr.getArray().push_back("apple");
    arr.getArray().push_back("banana");
    arr.getArray().push_back("cherry");
    arr.getArray().push_back(42);
    arr.getArray().push_back(true);
    
    std::cout << "Created JSON array:" << std::endl;
    std::cout << arr.toStringPretty() << std::endl;
}

// Example 4: Access and modify values
void example_access_modify()
{
    std::cout << "\n=== Example 4: Accessing and Modifying Values ===" << std::endl;
    
    Json obj;
    obj["users"] = Json();
    obj["users"].setArray();
    
    // Add user objects to array
    Json user1;
    user1["id"] = 1;
    user1["name"] = "Alice";
    user1["role"] = "admin";
    
    Json user2;
    user2["id"] = 2;
    user2["name"] = "Bob";
    user2["role"] = "user";
    
    obj["users"].getArray().push_back(user1);
    obj["users"].getArray().push_back(user2);
    
    std::cout << "Before modification:" << std::endl;
    std::cout << obj.toStringPretty() << std::endl;
    
    // Access and modify values
    if (obj["users"].isArray() && !obj["users"].getArray().empty()) {
        Json& first_user = obj["users"][0];
        if (first_user.contains("role")) {
            first_user["role"] = "superadmin";
        }
        first_user["permissions"] = Json();
        first_user["permissions"].setArray();
        first_user["permissions"].getArray().push_back("read");
        first_user["permissions"].getArray().push_back("write");
        first_user["permissions"].getArray().push_back("delete");
    }
    
    std::cout << "\nAfter modification:" << std::endl;
    std::cout << obj.toStringPretty() << std::endl;
}

// Example 5: Type checking and safe access
void example_type_checking()
{
    std::cout << "\n=== Example 5: Type Checking ===" << std::endl;
    
    Json obj;
    obj["string_value"] = "hello";
    obj["int_value"] = 42;
    obj["float_value"] = 3.14f;
    obj["double_value"] = 2.71828;
    obj["bool_value"] = true;
    obj["null_value"] = nullptr;
    obj["array_value"] = Json();
    obj["array_value"].setArray();
    obj["object_value"] = Json();
    obj["object_value"].setObject();
    
    std::cout << "Type checking results:" << std::endl;
    
    if (obj["string_value"].isString()) {
        std::cout << "  string_value: " << obj["string_value"].getString() << " (String)" << std::endl;
    }
    
    if (obj["int_value"].isLong()) {
        std::cout << "  int_value: " << obj["int_value"].getLong() << " (Long)" << std::endl;
    }
    
    if (obj["float_value"].isFloat()) {
        std::cout << "  float_value: " << obj["float_value"].getFloat() << " (Float)" << std::endl;
    }
    
    if (obj["double_value"].isDouble()) {
        std::cout << "  double_value: " << obj["double_value"].getDouble() << " (Double)" << std::endl;
    }
    
    if (obj["bool_value"].isBool()) {
        std::cout << "  bool_value: " << (obj["bool_value"].getBool() ? "true" : "false") << " (Bool)" << std::endl;
    }
    
    if (obj["null_value"].isNull()) {
        std::cout << "  null_value: null (Null)" << std::endl;
    }
    
    if (obj["array_value"].isArray()) {
        std::cout << "  array_value: [] (Array)" << std::endl;
    }
    
    if (obj["object_value"].isObject()) {
        std::cout << "  object_value: {} (Object)" << std::endl;
    }
}

// Example 6: Error handling
void example_error_handling()
{
    std::cout << "\n=== Example 6: Error Handling ===" << std::endl;
    
    // Valid JSON
    std::string valid_json = R"({"key": "value"})";
    auto result1 = Json::parse(valid_json);
    Json::Status status1 = result1.first;
    Json json1 = result1.second;
    
    if (status1 == Json::success) {
        std::cout << "Valid JSON parsed successfully" << std::endl;
    } else {
        std::cout << "Parse error: " << Json::StatusToString(status1) << std::endl;
    }
    
    // Invalid JSON - missing closing brace
    std::string invalid_json = R"({"key": "value")";
    auto result2 = Json::parse(invalid_json);
    Json::Status status2 = result2.first;
    Json json2 = result2.second;
    
    if (status2 == Json::success) {
        std::cout << "JSON parsed successfully (unexpected!)" << std::endl;
    } else {
        std::cout << "Parse error (expected): " << Json::StatusToString(status2) << std::endl;
    }
    
    // Invalid JSON - trailing comma
    std::string invalid_json2 = R"({"a": 1, "b": 2,})";
    auto result3 = Json::parse(invalid_json2);
    Json::Status status3 = result3.first;
    Json json3 = result3.second;
    
    if (status3 == Json::success) {
        std::cout << "JSON parsed successfully (unexpected!)" << std::endl;
    } else {
        std::cout << "Parse error (expected): " << Json::StatusToString(status3) << std::endl;
    }
}

// Example 7: Nested structures
void example_nested()
{
    std::cout << "\n=== Example 7: Nested Structures ===" << std::endl;
    
    Json config;
    config["database"] = Json();
    config["database"]["host"] = "localhost";
    config["database"]["port"] = 5432;
    config["database"]["credentials"] = Json();
    config["database"]["credentials"]["username"] = "admin";
    config["database"]["credentials"]["password"] = "secret123";
    
    config["server"] = Json();
    config["server"]["host"] = "0.0.0.0";
    config["server"]["port"] = 8080;
    config["server"]["ssl"] = true;
    
    config["features"] = Json();
    config["features"].setArray();
    config["features"].getArray().push_back("logging");
    config["features"].getArray().push_back("metrics");
    config["features"].getArray().push_back("caching");
    
    std::cout << "Nested configuration:" << std::endl;
    std::cout << config.toStringPretty() << std::endl;
}

// Example 8: Compact vs Pretty printing
void example_printing()
{
    std::cout << "\n=== Example 8: Compact vs Pretty Printing ===" << std::endl;
    
    Json obj;
    obj["name"] = "Test";
    obj["values"] = Json();
    obj["values"].setArray();
    obj["values"].getArray().push_back(1);
    obj["values"].getArray().push_back(2);
    obj["values"].getArray().push_back(3);
    
    std::cout << "Compact format:" << std::endl;
    std::cout << obj.toString() << std::endl;
    
    std::cout << "\nPretty format:" << std::endl;
    std::cout << obj.toStringPretty() << std::endl;
}

int main()
{
    std::cout << "JSON Library Example Program" << std::endl;
    std::cout << "============================" << std::endl;
    
    example_parse();
    example_create_object();
    example_create_array();
    example_access_modify();
    example_type_checking();
    example_error_handling();
    example_nested();
    example_printing();
    
    std::cout << "\nAll examples completed!" << std::endl;
    return 0;
}
