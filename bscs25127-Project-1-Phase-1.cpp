// ======================= TIME-TRAVEL DEBUGGER - SERVER TEMPLATE =======================

// Pipeline this file implements, top to bottom:
//   0. Receive  -- stream the client's .trace bytes straight to source.bin on disk
//   1. Pass 0X0   -- validity check (FUNC/FUNC_END matching)
//   2. Pass 0X1   -- resolve(): copy EVERY source line into resolve.bin as [offset][size][string], then patch CALL targets.
//   3. Pass 0X2   -- execute resolve.bin: tokenize ONE line at a time, update the call stack, take a snapshot -> Timeline
//   4. Pass 0X3   -- serialize Timeline -> session.tdbg(header + snapshot records + dense index)


#include <iostream>
#include <string>
#include <cstdint>
#include <fstream>
//#include <unistd.h>
//#include <sys/socket.h>
#include <cstdint>
#include <cstdio>
using namespace std;

// ---- Constants ----
const int32_t MAX_VARS_PER_FRAME = 16;
const int32_t MAX_STACK_DEPTH = 64;
const int32_t MAX_FUNCS = 128;
const int32_t MAX_TOKENS = MAX_VARS_PER_FRAME + 2; // kW + func_name + upto 16 params/args
const int32_t MAX_PATCHES = MAX_FUNCS * 4;
const uint64_t MAX_SOURCE_BYTES = 15ULL * 1024 * 1024; // sanity cap on the declared file length
const int32_t IO_BUFFER_SIZE = 64 * 1024;                  // fixed buffer for streaming to/from disk
const int32_t SOCKET_TIMEOUT_SEC = 5;                      // TODO: apply as SO_RCVTIMEO so a deadclient can't hang the server forever

// ---- Custom data structures

// Stack: back the live Call Stack during execution
template <typename T>
class Stack
{
    struct Node
    {
        T data;
        Node* next;
    };
    Node* top;
    int32_t count;

public:
    // Implement these functions:
    Stack()
    { // initialize the stack
        top = nullptr;
        count = 0;
    }
    void push(const T& val)
    {
        if (count >= MAX_STACK_DEPTH) throw overflow_error("Stack is full.");
        count++;
        Node* temp = top;
        top = new Node;
        top->data = val;
        top->next = temp;
        // pushes the value on the stack if max limit is not reached yet.
    }
    T pop()
    {
        if (count == 0) throw underflow_error("Stack is empty.");
        count--;
        T ret = top->data;
        Node* temp = top;
        top = top->next;
        delete temp;
        return ret;
        // pop the top value on the stack
    }
    T& peek()
    {
        if (count == 0) throw underflow_error("Stack is empty.");
        return top->data;
        // returns the top value on the stack
    }
    bool isEmpty()
    {
        return count == 0;
    }
    int32_t depth()
    {
        return count;
    }
    int32_t snapshot_into(T out[], int32_t maxLen)
    {
        // copies every frame, top to bottom in the array given as a parameter
        // this is what buildSnapshot() call, returns count written
        int i = 0;
        for (Node* n = top; n != nullptr && i < maxLen; i++) {
            out[i] = n->data;
            n = n->next;
        }
        return i;
    }
};


// Timeline : doubly linked list of Snapshots
struct Snapshot; // fwd declaration;
struct TimelineNode
{
    Snapshot* data;
    TimelineNode* next;
    TimelineNode* prev;
};
class Timeline
{
    TimelineNode* head, * tail;
    int32_t stepCount;

public:
    // Implement these functions
    Timeline()
    {
        head = tail = nullptr;
        stepCount = 0;
    }
    void record(Snapshot* s)
    {
        TimelineNode* n = new TimelineNode;
        n->data = s;
        n->next = nullptr;
        n->prev = nullptr;
        if (stepCount == 0) {
            head = tail = n;
        }
        else {
            TimelineNode* temp = tail;
            tail = n;
            n->prev = temp;
            temp->next = n;
        }
        stepCount++;
        // add record in the timeline
    }
    TimelineNode* begin()
    {
        return head;
    }
    int32_t getStepCount()
    {
        return stepCount;
    }
};

// Core structs
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
void writeHeader(FILE* f, const TTDBHeader& h)
{
    fwrite(h.magic, 1, 4, f);
    fwrite(&h.version, sizeof(int32_t), 1, f);

    // placeholder for other two data members
}

// resolve.bin - bookkeeping
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



// PASS 0x0: READING source.bin + VALIDITY CHECK
bool readSourceLine(ifstream& in, string& out)
{
    // reads the next nonblank line
    int ct;
    while (in.read(reinterpret_cast<char*>(&ct), sizeof(ct))) {
        out.resize(ct);
        in.read(&out[0], ct);
        if (out.find_first_not_of(" \n\t\r") == string::npos) {
            return true;
        }
    }
    return false;
}
string firstWord(const string& line)
{
    string out;
    for (int i = 0; i < line.length(); i++) {
        if (line[i] == ' ') break;
        out += line[i];
    }
    return out;
    // returns first word from the input string
}
string secondWord(const string& line)
{
    string out;
    int i = 0; 
    for (; i < line.length() && line[i] != ' '; i++);
    if (i == line.length()) return out;
    i++;
    for (; i < line.length(); i++) {
        if (line[i] == ' ') break;
        out += line[i];
    }
    return out;
    // returns the second word
}
bool validateProgram(const char* sourcePath)
{
    ifstream in;
    in.open(sourcePath, ios::binary);
    if (!in.is_open()) return false;
    Stack<string> s;
    string temp;
    while (readSourceLine(in, temp)) {
        string first = firstWord(temp);
        if (first == "func") {
            if (s.isEmpty()) {
                s.push("func");
                continue;
            }
            else return false;
        }
        else if (first == "func_end") {
            if (s.isEmpty()) return false;
            else s.pop();
        }
        temp.clear();
    }
    in.close();
    return s.isEmpty();
    // for each func defined there should be exactly one func_end and no nested funcs allowed - 
}

// PASS 0x1: RESOLVE() -> resolve.bin
int64_t writeResolveRecord(FILE* f, int64_t offsetField, const string& text)
{
    int32_t len = text.length();
    fwrite(&offsetField, sizeof(int64_t), 1, f);
    fwrite(&len, sizeof(int32_t), 1, f);
    fwrite(text.c_str(), sizeof(char), len, f);
    return 8 + 4 + len;
    // writes one [offset(8B)][size(4B)][string] record at the current file position
    // returns the number of bytes written
}
int64_t readResolveRecord(FILE* f, string& outText)
{
    int64_t offset;
    fread(&offset, sizeof(int64_t), 1, f);
    int32_t len;
    fread(&len, sizeof(int32_t), 1, f);
    outText.resize(len);
    fread(&outText[0], sizeof(char), len, f);
    return offset;
    // reads one record at the current position and advances past it, returns the offset field - the raw line text comes back untouched in outText.
}
int64_t resolveProgram(const char* sourcePath, const char* resolveBinPath)  
{
    FuncEntry funcArray[MAX_FUNCS];
    int32_t funcCount = 0;
    PendingPatch patches[MAX_PATCHES];
    int32_t patchCount = 0;

    ifstream in;
    in.open(sourcePath, ios::binary);
    if (!in.is_open()) throw runtime_error("Error : File not found.");

    FILE* file = fopen(resolveBinPath, "wb+");

    if (!file) throw runtime_error("Error : Resolve.bin could not be opened.");

    int64_t offset = 0;

    string temp, first;

    while (readSourceLine(in, temp)) {
        int64_t bytesWritten = writeResolveRecord(file, -1, temp);
        first = firstWord(temp);
        if (first == "func") {
            funcArray[funcCount].funcName = secondWord(temp);
            funcArray[funcCount].byteOffsetInResolveBin = offset;
            funcCount++;
        }
        else if (first == "call") {
            patches[patchCount].byteOffsetOfOffsetField = offset;
            patches[patchCount].targetFuncName = secondWord(temp);
            patchCount++;
        }
        offset += bytesWritten;
        temp.clear();
    }
    in.close();

    bool flag;

    for (int32_t i = 0; i < patchCount; i++) {
        flag = false;
        for (int32_t j = 0; j < funcCount; j++) {
            if (patches[i].targetFuncName == funcArray[j].funcName) {
                flag = true;
                fseek(file, patches[i].byteOffsetOfOffsetField, 0);
                fwrite(&funcArray[j].byteOffsetInResolveBin, sizeof(int64_t), 1, file);
                break;
            }
        }
        if (!flag) {
            fclose(file);
            throw runtime_error("Error : Called function not found.");
        }
    }

    offset = -1;

    for (int32_t i = 0; i < funcCount; i++) {
        if (funcArray[i].funcName == "main") {
            offset = funcArray[i].byteOffsetInResolveBin;
            break;
        }
    }

    if (offset == -1) {
        fclose(file);
        throw runtime_error("Error : No main defined.");
    }

    fclose(file);

    return offset;
    // Every source line becomes one record holding the raw line, as-is.
    // resolve() only PEEKS at the leading word(s) -- enough to spot FUNC
    // (remember its position) and CALL (remember which function it needs
    // and where its offset field sits).
    // Once the whole file is written, every CALL's offset field is patched
    // with its target's position. Patching happens after the full write
    // Returns the byte offset of main's FUNC header record.
    // if there is no main return the error 
}

// PASS 0x2: EXECUTION (tokenization happens here)
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
Snapshot* buildSnapshot(Stack<Frame>& callStack)
{
    // build the snapshot based on the callStack given
    Snapshot* ret = new Snapshot;
    ret->stackDepth = callStack.snapshot_into(ret->callStack, MAX_STACK_DEPTH);
    return ret;
}
Variable* findVariable(Frame* currFrame, const string& varName) {
    for (int i = 0; i < currFrame->localCount; i++) {
        if (currFrame->locals[i].name == varName) return &currFrame->locals[i];
    }
    for (int i = 0; i < currFrame->argc; i++) {
        if (currFrame->argv[i].name == varName) return &currFrame->argv[i];
    }
    return nullptr;
}
void executeProgram(const char* resolveBinPath, int64_t mainOffset, Timeline& timeline)
{
    // initialize the call stack
    // make the main frame
    // push main frame on the call stack
    Stack<Frame> callStack;
    Frame temp;
    temp.argc = 0; 
    temp.localCount = 0;
    temp.returnLine = -1;
    callStack.push(temp);
    FILE* file = fopen(resolveBinPath, "rb");
    if (!file) throw runtime_error("File could not be opened.");
    fseek(file, mainOffset, 0);
    string str;
    while (!feof(file)) {
        int64_t offset = readResolveRecord(file, str);
        if (str.empty()) break;
        Token tokens[MAX_TOKENS];
        int tokCt;
        try {
            tokCt = tokenizeLine(str, tokens, MAX_TOKENS);
            
            if (tokens[0].text == "set") {
                Frame& currFrame = callStack.peek();
                Variable* currVariable = findVariable(&currFrame, tokens[1].text);
                int val = stoi(tokens[2].text);
                if (currVariable != nullptr) {
                    currVariable->value = val;
                }
                else {
                    if (currFrame.localCount >= MAX_VARS_PER_FRAME) throw runtime_error("Error : Maximum Local Variables Limit Exceeded.");
                    currFrame.locals[currFrame.localCount].name = tokens[1].text;
                    currFrame.locals[currFrame.localCount].value = val;
                    currFrame.localCount++;
                }
            }
            else if (tokens[0].text == "add") {
                Frame& currFrame = callStack.peek();
                Variable* a = findVariable(&currFrame, tokens[1].text);
                Variable* b = findVariable(&currFrame, tokens[2].text);
                if (a == nullptr) throw runtime_error("Error : Variable " + tokens[1].text + " is not defined.");
                int val;
                if (b == nullptr) val = stoi(tokens[2].text);
                else val = b->value;
                a->value += val;
            }
            else if (tokens[0].text == "sub") {
                Frame& currFrame = callStack.peek();
                Variable* a = findVariable(&currFrame, tokens[1].text);
                Variable* b = findVariable(&currFrame, tokens[2].text);
                if (a == nullptr) throw runtime_error("Error : Variable " + tokens[1].text + " is not defined.");
                int val;
                if (b == nullptr) val = stoi(tokens[2].text);
                else val = b->value;
                a->value -= val;
            }
            else if (tokens[0].text == "mul") {
                Frame& currFrame = callStack.peek();
                Variable* a = findVariable(&currFrame, tokens[1].text);
                Variable* b = findVariable(&currFrame, tokens[2].text);
                if (a == nullptr) throw runtime_error("Error : Variable " + tokens[1].text + " is not defined.");
                int val;
                if (b == nullptr) val = stoi(tokens[2].text);
                else val = b->value;
                a->value *= val;
            }
            else if (tokens[0].text == "div") {
                Frame& currFrame = callStack.peek();
                Variable* a = findVariable(&currFrame, tokens[1].text);
                Variable* b = findVariable(&currFrame, tokens[2].text);
                if (a == nullptr) throw runtime_error("Error : Variable " + tokens[1].text + " is not defined.");
                int val;
                if (b == nullptr) val = stoi(tokens[2].text);
                else val = b->value;
                if (val == 0) throw runtime_error("Error : Division by 0 is undefined.");
                a->value /= val;
            }
            else if (tokens[0].text == "call") {
                Frame newFrame;
                newFrame.func_name = tokens[1].text;
                newFrame.argc = 0;
                newFrame.localCount = 0;
                newFrame.returnLine = ftell(file);
                Frame& currFrame = callStack.peek();
                for (int i = 2; i < tokCt; i++) {
                    Variable* argVar = findVariable(&currFrame, tokens[i].text);
                    int val;
                    if (argVar == nullptr) val = stoi(tokens[i].text);
                    else val = argVar->value;
                    newFrame.argv[newFrame.argc++].value = val;
                }
                callStack.push(newFrame);
                fseek(file, offset, 0);
            }
            else if (tokens[0].text == "func") {
                Frame& currFrame = callStack.peek();
                
                for (int i = 2; i < tokCt; i++) {
                    currFrame.argv[i - 2].name = tokens[i].text;
                }
            }
            else if (tokens[0].text == "func_end") {
                Frame poppedFrame = callStack.pop();

                if (poppedFrame.returnLine == -1) break;

                fseek(file, poppedFrame.returnLine, 0);
            }
            timeline.record(buildSnapshot(callStack));
        }
        catch (const std::exception& e) {
            cout << e.what() << endl;
            return;
        }
    }

    // implementation:
    // execute line by line, and according to the keyword perform action
}

// PASS 0x3: SERIALIZE TIMELINE
void writeTdbg(Timeline& timeline, const char* tdbgPath)
{
    // placeholder for header
    // index array of the size of stepcount from the timeline
    // placing each snapshot in the file while maintaining the index(starting point of each nth snapshot)
    // after timeline add the index array i the file
    // update the header
}
// main section
int32_t main()
{
    try {
        if (!validateProgram("source.bin"))
        {
            // send an error response instead of a .tdbg file
            return 1;
        }
    }
    catch (const std::exception& e) {
        cout << e.what() << endl;
        return -1;
    }
    
    try {
        int64_t mainOffset = resolveProgram("source.bin", "resolve.bin");
        Timeline timeline;
        executeProgram("resolve.bin", mainOffset, timeline);

        writeTdbg(timeline, "session.tdbg");

        return 0;
    }
    catch (const std::exception& e) {
        cout << e.what() << endl;
        return -1;
    }
}