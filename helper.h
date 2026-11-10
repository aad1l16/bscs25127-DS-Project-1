#pragma once
#include <string>
#include <iostream>
#include <cstdint>
#include <sys/socket.h>
#include <stdexcept>
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

inline int32_t tokenizeLine(const string& line, Token tokens[], int32_t maxTokens)
{
    // first word is always a instruction keyword
    // instruction set = [func, func_end, call, set, add, sub, mul and div]
    // next word is identifier like name of a function, variable name
    // after identifier all are the params/arg, space separated
    int idx = 0;
    string temp;
    for (int i = 0; line[i] != '\0'; i++) {
        if (idx == maxTokens) throw std::runtime_error("Error : Maximum Token limit exceeded.");
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
        if (idx == maxTokens) throw std::runtime_error("Error : Maximum Token limit exceeded.");
        if (idx == 0) tokens[idx].type = KEYWORD;
        if (idx == 1) tokens[idx].type = IDENTIFIER;
        if (idx > 1) tokens[idx].type = PARAM;
        tokens[idx++].text = temp;
    }
    return idx;
}

inline bool sendAll(int sock, const char *buf, size_t n)
{
    // send() can send fewer bytes than asked for, so keep calling it until all n bytes are sent.
    // Use the flag MSG_NOSIGNAL. Return false if the connection fails.
    size_t ct = 0;
    while(ct < n){
        int temp = send(sock, buf + ct, n - ct, MSG_NOSIGNAL);
        if(temp <= 0) return false;
        ct += (size_t)temp;
    }
    return true;
}

inline uint32_t initCrc32() {
    return 0xFFFFFFFF;
}

inline uint32_t updateCrc32(uint32_t crc, const char* data, size_t length) {
    static uint32_t table[256];
    static bool tableComputed = false;

    if (!tableComputed) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int j = 0; j < 8; ++j) {
                c = (c & 1) ? (0xEDB88320 ^ (c >> 1)) : (c >> 1);
            }
            table[i] = c;
        }
        tableComputed = true;
    }

    for (size_t i = 0; i < length; ++i) {
        crc = table[(crc ^ static_cast<unsigned char>(data[i])) & 0xFF] ^ (crc >> 8);
    }
    return crc;
}

inline uint32_t finalizeCrc32(uint32_t crc) {
    return ~crc;
}

template <typename T>
class Queue
{
    struct Node
    {
        T data;
        Node *next;
        Node(T d) : data{ d }, next{ nullptr } {}
    };
    Node *front, *rear;
    int count;


public:
    // Implement these functions:
    Queue()
    {
        front = rear = nullptr;
        count = 0;
    }
    ~Queue()
    {
        while(!isEmpty()){
            dequeue();
        }
    }
    void enqueue(const T &val)
    {
        Node* node = new Node(val);
        if(!front){
            front = rear = node;
        }
        else{
            rear->next = node;
            rear = node;
        }
        count++;
    }
    T dequeue()
    {
        if(!front) throw std::underflow_error("Queue is empty.");
        Node* node = front;
        front = front->next;
        if(!front){
            rear = nullptr;
        }
        count--;
        T val = node->data;
        delete node;
        return val;
    }
    bool isEmpty()
    {
        return count == 0;
    }
    int size()
    {
        return count;
    }
};

inline bool sendError(int sock, const string &message)
{
    // Reply format: [status: 1 byte][payload until close][checksum: 4 bytes].
    // For an error: send one byte with value 1, then the message text. 
    char status = 1;
    uint32_t crc = initCrc32();
    crc = updateCrc32(crc, &status, 1);
    crc = updateCrc32(crc, message.data(), message.size());
    uint32_t finalCrc = finalizeCrc32(crc);

    if(!sendAll(sock, &status, 1)){
        return false;
    }
    if(!sendAll(sock, message.data(), message.size())){
        return false;
    }
    if(!sendAll(sock, reinterpret_cast<const char*>(&finalCrc), sizeof(finalCrc))){
        return false;
    }
    return true;
}