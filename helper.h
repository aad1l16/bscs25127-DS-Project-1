#pragma once
#include <string>
#include <iostream>
using namespace std;

// Helper.h should contain the structs, functions, constants, etc. that are used more than one time in the project. For example, the structs that are used in both server and client, the constants that are used in all three files. 
const int32_t MAX_VARS_PER_FRAME = 16;
const int32_t MAX_STACK_DEPTH = 64;
struct FuncEntry
{
    string funcName;
    int64_t byteOffsetInResolveBin; // where this function's FUNC header record sits
};
struct PendingPatch
{
    int64_t byteOffsetOfOffsetField; // where in resolve.bin to seek back and overwrite
    string targetFuncName;
};
struct Variable
{
    string name;
    int32_t value;
};
struct Frame
{
    string func_name;
    int32_t argc;
    Variable argv[MAX_VARS_PER_FRAME];
    int32_t returnLine;
    Variable locals[MAX_VARS_PER_FRAME];
    int32_t localCount;
};
struct Snapshot
{
    Frame callStack[MAX_STACK_DEPTH];
    int32_t stackDepth;
};
struct TTDBHeader 
{
    char magic[4]; // "TTDB"
    int32_t version;
    int32_t stepCount;
    int64_t indexOffset;
};
enum TokenType
{
    KEYWORD,
    IDENTIFIER,
    PARAM
};
struct Token
{
    TokenType type;
    string text;
};
int32_t tokenizeLine(const string& line, Token tokens[], int32_t maxTokens)
{
    // first word is always a instruction keyword
    // instruction set = [func, func_end, call, set, add, sub, mul and div]
    // next word is identifier like name of a function, variable name
    // after identifier all are the params/arg, space separated
    int idx = 0;
    string temp;
    for (int i = 0; line[i] != '\0'; i++) {
        if (idx == maxTokens) throw runtime_error("Error : Maximum Token limit exceeded.");
        if (line[i] == ' ') {
            if(temp.empty()) continue;
            if (idx == 0) tokens[idx].type = KEYWORD;
            if (idx == 1) tokens[idx].type = IDENTIFIER;
            if(idx > 1) tokens[idx].type = PARAM;
            tokens[idx++].text = temp;
            temp.clear();
            continue;
        }
        temp += line[i];
    }
    if (!temp.empty()) {
        if (idx == maxTokens) throw runtime_error("Error : Maximum Token limit exceeded.");
        if (idx == 0) tokens[idx].type = KEYWORD;
        if (idx == 1) tokens[idx].type = IDENTIFIER;
        if (idx > 1) tokens[idx].type = PARAM;
        tokens[idx++].text = temp;
    }
    return idx;
}
