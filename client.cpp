// ======================= TIME-TRAVEL DEBUGGER - CLIENT TEMPLATE =======================

// The client never talks to the server directly. It only talks to the load balancer.
// The load balancer wakes the server up if it is down, so the client does not care.
// The first "start" may just take a bit longer.
//
// The network is used ONLY by "start". After the .tdbg file is downloaded,
// next / prv / jmp read that file on the local disk. They never contact the load balancer.
//
// How the pieces fit together:
//   main loop -> readCommand -> runCommand
//   start -> cmdStart()  -> requestDebugFile() (Part A) -> openTdbg() (Part B) -> shows step 1
//   next, prv, jmp -> moveBy() (Part D) -> readSnapshotAt() (Part B) -> printSnapshot() (Part E)

#include <iostream>
#include <string>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <signal.h>
#include <vector>
#include <sys/socket.h>
#include "helper.h"
using namespace std;

// ---- Constants ----
const char *PROGRAM_NAME = "ttdb";
const char *PROMPT = "ttdb> ";
const int IO_BUFFER_SIZE = 64 * 1024;     // fixed buffer for streaming files to and from the socket
const char *TDBG_FILE = "ttdb_last.tdbg"; // where the downloaded .tdbg is saved
const int MAX_JUMP = 1000000000;          // biggest n accepted by jmp
string LB_HOST = "127.0.0.0";
int LB_PORT = 5000;


// ---- Structs only the client needs ----
struct TdbgReader
{
    ssize_t file_index;
    TTDBHeader header;
    vector<int64_t> index;
};
struct DebugSession
{
    int active_flag;     // false until a "start" has worked
    TdbgReader reader;   // the .tdbg file stays open for the whole session
    int64_t currentStep; 
};

// =============================================================================================
// PART A: TALKING TO THE LOAD BALANCER
// =============================================================================================

bool recvAll(int sock, char *buf, size_t n)
{
    // recv() can return fewer bytes than asked for, so keep calling it until exactly n bytes are read.
    // Return false if the connection closes or fails before n bytes arrive.
}
bool sendAll(int sock, const char *buf, size_t n)
{
    // send() can send fewer bytes than asked for, so keep calling it until all n bytes are sent.
    // Use the flag MSG_NOSIGNAL. Return false if the connection fails.
}

int connectToHost(const char *host, int port)
{
    // Create a TCP socket and connect it to host:port.
    // Return the socket, or -1 if the connection fails.
}
bool uploadSource(int sock, const char *sourcePath)
{
    // have to do checksum and then send
    // When the file is finished, call shutdown(sock, SHUT_WR). This is how the server knows the string is complete.
    // Return false if the file cannot be opened or any send fails.
}
bool receiveResponse(int sock, const char *outTdbgPath, string &outError)
{
    // Reply on the wire: [status][payload][CRC-32]. The CRC covers the status byte too.
    // Feed every received byte (status included) to a TrailerChecker.
    // Status 1: collect the payload into outError. Status 0: write the payload to the file.
    // At the end call verify(). If it fails, delete the file, set outError to
    // "checksum mismatch, response corrupted" and return false.
}

// =============================================================================================
// PART B: READING THE .tdbg FILE
// File layout (written by Phase 1):
//   header | snapshot 0 | snapshot 1 | ... | index (one 8 byte offset per snapshot)
// The index lets us jump straight to any step without reading the steps before it.
// =============================================================================================

bool openTdbg(const char *path, TdbgReader &r)
{
    FILE* file = fopen(path, "rb");
    if(!file) return false;
    fread(&r.header.magic, sizeof(char), 4, file);
    if(strncmp(r.header.magic, "TTDB", 4) != 0){
        fclose(file);
        return false;
    }
    fread(&r.header.version, sizeof(int32_t), 1, file);
    fread(&r.header.stepCount, sizeof(int32_t), 1, file);
    fread(&r.header.indexOffset, sizeof(int64_t), 1, file);
    fseek(file, r.header.indexOffset, 0);
    r.index.resize(r.header.stepCount); 
    fread(r.index.data(), sizeof(int64_t), r.header.stepCount, file);
    r.file_index = (ssize_t)file;
    return true;
}
void closeTdbg(TdbgReader &r)
{
    // Free r.index and close the file.
    r.index.clear();
    FILE* file = (FILE*)r.file_index;
    if (file) {
        fclose(file);
        r.file_index = -1;
    }
}
Snapshot *readSnapshotAt(TdbgReader &r, int32_t step)
{
    FILE* file = (FILE*)r.file_index;
    fseek(file, r.index[step], 0);
    Snapshot* s = new Snapshot;
    if(fread(&s->stackDepth, sizeof(int32_t), 1, file) != 1){
        delete s;
        return nullptr;
    }
    for(int i = 0; i < s->stackDepth; i++){
        int32_t len;
        fread(&len, sizeof(int32_t), 1, file);
        s->callStack[i].func_name.resize(len);
        fread(&s->callStack[i].func_name[0], sizeof(char), len, file);
        fread(&s->callStack[i].argc, sizeof(int32_t), 1, file);
        for(int j = 0; j < s->callStack[i].argc; j++){
            fread(&len, sizeof(int32_t), 1, file);
            s->callStack[i].argv[j].name.resize(len);
            fread(&s->callStack[i].argv[j].name[0], sizeof(char), len, file);
            fread(&s->callStack[i].argv[j].value, sizeof(int32_t), 1, file);
        }
        fread(&s->callStack[i].returnLine, sizeof(int32_t), 1, file);
        fread(&s->callStack[i].localCount, sizeof(int32_t), 1, file);
        for(int j = 0; j < s->callStack[i].localCount; j++){
            fread(&len, sizeof(int32_t), 1, file);
            s->callStack[i].locals[j].name.resize(len);
            fread(&s->callStack[i].locals[j].name[0], sizeof(char), len, file);
            fread(&s->callStack[i].locals[j].value, sizeof(int32_t), 1, file);
        }
    }
    return s;
}

// =============================================================================================
// PART C: THE SESSION IN MEMORY
// There is one DebugSession variable, created in main() and passed to every command by reference.
// =============================================================================================

void initSession(DebugSession &s)
{
   
}
void closeSession(DebugSession &s)
{
    // If active, call closeTdbg(s.reader). Then set active = false.
    // Called when the user types quit and when the input ends.
}

// =============================================================================================
// PART D: THE COMMANDS
// =============================================================================================

void printSnapshot(int32_t step, int32_t totalSteps, const Snapshot &s); // defined in Part E

bool showCurrent(DebugSession &s)
{
    // Read the snapshot at s.currentStep, call printSnapshot, delete the snapshot.
    // If readSnapshotAt returns nullptr, print "damaged .tdbg file" and return false.\
    
}
bool moveBy(DebugSession &s, int32_t delta)
{
    if(!s.active_flag) return false;
    int targetStep = s.currentStep + delta;
    if(targetStep < 0 || targetStep >= s.reader.header.stepCount) return false;
    s.currentStep = targetStep;
    showCurrent(s);
    return true;
}
bool cmdStart(DebugSession &s, const string &sourcePath)
{
    // 1. requestDebugFile with host and port, saving to TDBG_FILE.
    // 2. If it fails, print the error text and return false. Keep the old session as it was.
    // 3. Open the new file with openTdbg into a NEW local TdbgReader.
    //    If that fails, print "server sent a damaged .tdbg file", keep the old session, return false.
    // 4. Only now close the old reader (if s.active), then put the new reader into s,
    //    set s.currentStep = 0 and s.active = true.
    // 5. Show step 1 with showCurrent(s).
}
bool cmdNext(DebugSession &s) {
    return moveBy(s, 1);
}
bool cmdPrv(DebugSession &s) {
    return moveBy(s, -1);
}
bool cmdJmpNext(DebugSession &s, int32_t n) {
    return moveBy(s, n);
}
bool cmdJmpPrv(DebugSession &s, int32_t n) { 
    return moveBy(s, -n);
}

// =============================================================================================
// PART E: PRINTING
// =============================================================================================

void printSnapshot(int32_t step, int32_t totalSteps, const Snapshot &s)
{
}
void printHelp()
{
    // Print the commands
}

// =============================================================================================
// PART F: THE INFINITE LOOP
// =============================================================================================

bool runCommand(DebugSession &s, const string &line, bool &quitRequested)
{
    // 1. Split the line into words.
    // 2. If the first word is "ttdb", drop it.
    // 3. Match the first word:
    //      start      -> needs a path after it, then cmdStart
    //      next       -> no more words, then cmdNext
    //      prv        -> no more words, then cmdPrv
    //      jmp        -> second word "next" or "prv", third word a positive number ,
    //                    then cmdJmpNext or cmdJmpPrv
    //      help       -> printHelp
    //      quit, exit -> set quitRequested = true
    // 4. For anything else, or a wrong number of words, print "unknown command" and the help text, and return false.
}
int main(int argc, char *argv[])
{
    // you get string LB_HOST and int LB_PORT = 0 in arguments of this main function. 

    // DebugSession session; initSession(session);
    // Print a short welcome and the help text.
    //
    // Infinite loop:
    //   if quitRequested, break
    //
    // After the loop: closeSession(session) and return 0.
    // Never exit the loop because of an error. Only quit or the end of the input leaves it.

}