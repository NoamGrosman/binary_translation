#include "pin.H"
#include <iostream>
#include <fstream>
#include <string>
#include <algorithm>
#include <map>

// register tracking
static const int NUM_TRACKED_REGS = 6;
static const REG TRACKED_REGS[NUM_TRACKED_REGS] = {
    LEVEL_BASE::REG_RAX, LEVEL_BASE::REG_RBX, LEVEL_BASE::REG_RCX,
    LEVEL_BASE::REG_RDX, LEVEL_BASE::REG_RSI, LEVEL_BASE::REG_RDI
};
static const char* TRACKED_REG_NAMES[NUM_TRACKED_REGS] = {
    "RAX", "RBX", "RCX", "RDX", "RSI", "RDI"
};
static const int MAX_REG_SAMPLES = 20;

struct RegSamples {
    ADDRINT values[MAX_REG_SAMPLES];
    int count;
    RegSamples() : count(0) {}
};

struct RoutineData {
    std::string imageName;
    ADDRINT imageAddress;
    std::string routineName;
    ADDRINT routineAddress;
    UINT64 instructionCount;
    UINT64 callCount;
    RegSamples regs[NUM_TRACKED_REGS];

    RoutineData()
        : imageAddress(0), routineAddress(0),
          instructionCount(0), callCount(0) {}
};

static RoutineData routinesArray[1000];
static int totalRoutinesFound = 0;
static std::map<ADDRINT, RoutineData*> rtnAddrToData;

VOID CountRoutineCall(UINT64* callCountPtr) {
    (*callCountPtr)++;
}

VOID CountBblInstructions(UINT64* instructionCountPtr, UINT32 numIns) {
    (*instructionCountPtr) += numIns;
}

VOID RecordRegValue(RegSamples* s, ADDRINT val) {
    if (s->count < MAX_REG_SAMPLES) {
        s->values[s->count++] = val;
    }
}

bool CompareRoutines(const RoutineData& a, const RoutineData& b) {
    return a.instructionCount > b.instructionCount;
}

VOID Routine(RTN rtn, VOID* v) {
    ADDRINT addr = RTN_Address(rtn);
    if (rtnAddrToData.find(addr) != rtnAddrToData.end()) return;
    if (totalRoutinesFound >= 1000) return;

    RoutineData* myData = &routinesArray[totalRoutinesFound];
    totalRoutinesFound++;
    rtnAddrToData[addr] = myData;

    myData->routineName = RTN_Name(rtn);
    myData->routineAddress = addr;

    SEC sec = RTN_Sec(rtn);
    if (SEC_Valid(sec)) {
        IMG img = SEC_Img(sec);
        if (IMG_Valid(img)) {
            myData->imageName = IMG_Name(img);
            myData->imageAddress = IMG_LowAddress(img);
        }
    }

    RTN_Open(rtn);
    RTN_InsertCall(rtn, IPOINT_BEFORE, (AFUNPTR)CountRoutineCall,
                   IARG_PTR, &(myData->callCount), IARG_END);
    RTN_Close(rtn);
}

VOID Trace(TRACE trace, VOID* v) {
    RTN rtn = TRACE_Rtn(trace);
    RoutineData* data = NULL;
    if (RTN_Valid(rtn)) {
        std::map<ADDRINT, RoutineData*>::iterator it = rtnAddrToData.find(RTN_Address(rtn));
        if (it != rtnAddrToData.end()) data = it->second;
    }

    for (BBL bbl = TRACE_BblHead(trace); BBL_Valid(bbl); bbl = BBL_Next(bbl)) {
        if (data) {
            BBL_InsertCall(bbl, IPOINT_ANYWHERE, (AFUNPTR)CountBblInstructions,
                           IARG_PTR, &(data->instructionCount),
                           IARG_UINT32, BBL_NumIns(bbl),
                           IARG_END);

            // per-routine register write tracking
            for (INS ins = BBL_InsHead(bbl); INS_Valid(ins); ins = INS_Next(ins)) {
                if (!INS_HasFallThrough(ins)) continue;
                for (int i = 0; i < NUM_TRACKED_REGS; i++) {
                    if (INS_RegWContain(ins, TRACKED_REGS[i])) {
                        INS_InsertCall(ins, IPOINT_AFTER, (AFUNPTR)RecordRegValue,
                                       IARG_PTR, &(data->regs[i]),
                                       IARG_REG_VALUE, TRACKED_REGS[i],
                                       IARG_END);
                    }
                }
            }
        }
    }
}

VOID FINI(INT32 code, VOID* v) {
    std::sort(routinesArray, routinesArray + totalRoutinesFound, CompareRoutines);
    std::ofstream outFile("rtn-output.csv");

    for (int i = 0; i < totalRoutinesFound; i++) {
        RoutineData& r = routinesArray[i];
        if (r.instructionCount == 0 && r.callCount == 0) continue;

        // routine info
        outFile << r.imageName << ", "
                << "0x" << std::hex << r.imageAddress << std::dec << ", "
                << r.routineName << ", "
                << "0x" << std::hex << r.routineAddress << std::dec << ", "
                << r.instructionCount << ", "
                << r.callCount;

        // 6 register sections in the same line
        for (int j = 0; j < NUM_TRACKED_REGS; j++) {
            const RegSamples& s = r.regs[j];
            outFile << ", " << TRACKED_REG_NAMES[j] << " values: ";
            for (int k = 0; k < s.count; k++) {
                outFile << "0x" << std::hex << s.values[k] << std::dec;
                if (k < s.count - 1) outFile << " -> ";
            }
            if (s.count >= 2) {
                UINT64 sumAbsDelta = 0;
                for (int k = 1; k < s.count; k++) {
                    INT64 d = (INT64)(s.values[k] - s.values[k - 1]);
                    sumAbsDelta += (d < 0) ? (UINT64)(-d) : (UINT64)d;
                }
                UINT64 avg = sumAbsDelta / (s.count - 1);
                outFile << "  Has an Average delta: Yes  Average delta: 0x"
                        << std::hex << avg << std::dec;
            } else {
                outFile << "  Has an Average delta: No";
            }
        }
        outFile << std::endl;
    }
    outFile.close();
}

int main(int argc, char* argv[]) {
    PIN_InitSymbols();
    if (PIN_Init(argc, argv)) {
        std::cerr << "Pin failed to initialize." << std::endl;
        return 1;
    }
    RTN_AddInstrumentFunction(Routine, 0);
    TRACE_AddInstrumentFunction(Trace, 0);
    PIN_AddFiniFunction(FINI, 0);
    PIN_StartProgram();
    return 0;
}
