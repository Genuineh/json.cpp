// -*- mode:c++;indent-tabs-mode:nil;c-basic-offset:4;coding:utf-8 -*-
// vi: set et ft=cpp ts=4 sts=4 sw=4 fenc=utf-8 :vi
//
// JSON ?????
// ???????????? JSON ??

#include "../json.h"
#include <iostream>
#include <iomanip>

using jt::Json;

void printSeparator(const char* title) {
    std::cout << "\n" << std::string(60, '=') << "\n";
    std::cout << title << "\n";
    std::cout << std::string(60, '=') << "\n";
}

// ?? 1: ?? JSON ???
void example1_parseJson() {
    printSeparator("?? 1: ?? JSON ???");
    
    const char* jsonStr = R"({
        "name": "??",
        "age": 25,
        "city": "??",
        "isStudent": false,
        "grades": [85, 92, 88, 95]
    })";
    
    auto [status, json] = Json::parse(jsonStr);
    
    if (status != Json::success) {
        std::cout << "????: " << Json::StatusToString(status) << "\n";
        return;
    }
    
    std::cout << "?????\n";
    std::cout << "JSON ??:\n" << json.toStringPretty() << "\n";
    
    // ????
    if (json.contains("name")) {
        std::cout << "??: " << json["name"].getString() << "\n";
    }
    if (json.contains("age")) {
        std::cout << "??: " << json["age"].getLong() << "\n";
    }
}

// ?? 2: ?? JSON ??
void example2_createJson() {
    printSeparator("?? 2: ?? JSON ??");
    
    Json person;
    person.setObject();
    person["name"] = "??";
    person["age"] = 30;
    person["height"] = 1.75;
    person["isMarried"] = true;
    
    // ????
    person["hobbies"].setArray();
    person["hobbies"].getArray().push_back("??");
    person["hobbies"].getArray().push_back("??");
    person["hobbies"].getArray().push_back("??");
    
    std::cout << "??? JSON ??:\n";
    std::cout << person.toStringPretty() << "\n";
}

// ?? 3: ????
void example3_workWithArray() {
    printSeparator("?? 3: ?? JSON ??");
    
    Json arr;
    arr.setArray();
    arr.getArray().push_back(1);
    arr.getArray().push_back(2);
    arr.getArray().push_back(3);
    arr.getArray().push_back("hello");
    arr.getArray().push_back(true);
    arr.getArray().push_back(nullptr);
    
    std::cout << "????: " << arr.toString() << "\n";
    std::cout << "?????:\n" << arr.toStringPretty() << "\n";
    
    std::cout << "\n??????:\n";
    for (size_t i = 0; i < arr.getArray().size(); i++) {
        Json& elem = arr[i];
        std::cout << "  [" << i << "] ";
        
        if (elem.isNull()) {
            std::cout << "null\n";
        } else if (elem.isBool()) {
            std::cout << (elem.getBool() ? "true" : "false") << "\n";
        } else if (elem.isString()) {
            std::cout << "\"" << elem.getString() << "\"\n";
        } else if (elem.isNumber()) {
            std::cout << elem.getNumber() << "\n";
        }
    }
}

// ?? 4: ???????
void example4_nestedStructure() {
    printSeparator("?? 4: ???????");
    
    Json company;
    company.setObject();
    company["name"] = "????";
    company["employees"].setArray();
    
    // ????
    Json emp1;
    emp1.setObject();
    emp1["name"] = "??";
    emp1["department"] = "???";
    emp1["salary"] = 15000;
    
    Json emp2;
    emp2.setObject();
    emp2["name"] = "??";
    emp2["department"] = "???";
    emp2["salary"] = 12000;
    
    company["employees"].getArray().push_back(emp1);
    company["employees"].getArray().push_back(emp2);
    
    std::cout << "????:\n" << company.toStringPretty() << "\n";
    
    // ??????
    std::cout << "\n????:\n";
    for (auto& emp : company["employees"].getArray()) {
        std::cout << "  - " << emp["name"].getString() 
                  << " (" << emp["department"].getString() << ")\n";
    }
}

// ?? 5: ????
void example5_typeChecking() {
    printSeparator("?? 5: JSON ????");
    
    Json json;
    json["nullValue"] = nullptr;
    json["boolValue"] = true;
    json["intValue"] = 42;
    json["floatValue"] = 3.14f;
    json["doubleValue"] = 2.71828;
    json["stringValue"] = "hello";
    
    std::cout << "????:\n";
    for (const auto& pair : json.getObject()) {
        const std::string& key = pair.first;
        const Json& value = pair.second;
        
        std::cout << "  " << key << ": ";
        if (value.isNull()) {
            std::cout << "Null";
        } else if (value.isBool()) {
            std::cout << "Bool";
        } else if (value.isLong()) {
            std::cout << "Long";
        } else if (value.isFloat()) {
            std::cout << "Float";
        } else if (value.isDouble()) {
            std::cout << "Double";
        } else if (value.isString()) {
            std::cout << "String";
        } else if (value.isArray()) {
            std::cout << "Array";
        } else if (value.isObject()) {
            std::cout << "Object";
        }
        std::cout << "\n";
    }
}

// ?? 6: ????
void example6_errorHandling() {
    printSeparator("?? 6: ????");
    
    // ????? JSON
    const char* invalidJson = "{name: \"test\"}";  // ????
    
    auto [status, json] = Json::parse(invalidJson);
    
    if (status != Json::success) {
        std::cout << "?????????: " << Json::StatusToString(status) << "\n";
    } else {
        std::cout << "????: " << json.toString() << "\n";
    }
    
    // ????? JSON
    const char* validJson = "{\"name\": \"test\"}";
    auto [status2, json2] = Json::parse(validJson);
    
    if (status2 != Json::success) {
        std::cout << "?????????: " << Json::StatusToString(status2) << "\n";
    } else {
        std::cout << "????: " << json2.toString() << "\n";
    }
}

// ?? 7: JSONPath ????????
void example7_jsonpath() {
    printSeparator("?? 7: JSONPath ??");
    
    const char* jsonStr = R"({
        "store": {
            "book": [
                {"title": "Java??", "price": 39.99},
                {"title": "C++??", "price": 29.99},
                {"title": "Python??", "price": 49.99}
            ]
        }
    })";
    
    auto [status, json] = Json::parse(jsonStr);
    if (status != Json::success) {
        std::cout << "????\n";
        return;
    }
    
    std::cout << "?? JSON:\n" << json.toStringPretty() << "\n";
    
    // ?? JSONPath ?????? 35 ???
    std::cout << "\n?????? 35 ???:\n";
    auto results = json.jsonpath("$.store.book[?(@.price > 35)]");
    
    for (Json* node : results) {
        if (node->isObject()) {
            auto& obj = node->getObject();
            if (obj.find("title") != obj.end() && obj.find("price") != obj.end()) {
                std::cout << "  - " << obj.at("title").getString() 
                          << ": ?" << obj.at("price").getDouble() << "\n";
            }
        }
    }
}

int main() {
    std::cout << "JSON ???????\n";
    std::cout << "?? jt::Json ??????\n";
    
    try {
        example1_parseJson();
        example2_createJson();
        example3_workWithArray();
        example4_nestedStructure();
        example5_typeChecking();
        example6_errorHandling();
        example7_jsonpath();
        
        std::cout << "\n" << std::string(60, '=') << "\n";
        std::cout << "?????????\n";
        std::cout << std::string(60, '=') << "\n";
    } catch (const std::exception& e) {
        std::cerr << "??: " << e.what() << "\n";
        return 1;
    }
    
    return 0;
}
