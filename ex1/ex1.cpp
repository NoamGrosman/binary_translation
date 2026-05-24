#include "pin.H"
#include <iostream>
#include <fstream>
#include <string>
#include <algorithm>
#include <map>

struct RoutineData {
    std::string imageName;
    ADDRINT imageAddress;
    std::string routineName;
    ADDRINT routineAddress;
    UINT64 instructionCount;
    UINT64 callCount;
};

static RoutineData routinesArray[1000];
static int totalRoutinesFound = 0;
static std::map<ADDRINT, RoutineData*> rtnAddrToData;

//register tracking
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
};
static RegSamples regSamples[NUM_TRACKED_REGS];

VOID CountRoutineCall(UINT64* callCountPtr) {
    (*callCountPtr)++;
}

VOID CountBblInstructions(UINT64* instructionCountPtr, UINT32 numIns) {
    (*instructionCountPtr) += numIns;
}

VOID RecordRegValue(UINT32 idx, ADDRINT val) {
    if (regSamples[idx].count < MAX_REG_SAMPLES) {
        regSamples[idx].values[regSamples[idx].count++] = val;
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
        }

        //instrument writes to tracked registers
        for (INS ins = BBL_InsHead(bbl); INS_Valid(ins); ins = INS_Next(ins)) {
            if (!INS_HasFallThrough(ins)) continue;
            for (int i = 0; i < NUM_TRACKED_REGS; i++) {
                if (INS_RegWContain(ins, TRACKED_REGS[i])) {
                    INS_InsertCall(ins, IPOINT_AFTER, (AFUNPTR)RecordRegValue,
                                   IARG_UINT32, (UINT32)i,
                                   IARG_REG_VALUE, TRACKED_REGS[i],
                                   IARG_END);
                }
            }
        }
    }
}

VOID FINI(INT32 code, VOID* v) {
    std::sort(routinesArray, routinesArray + totalRoutinesFound, CompareRoutines);
    std::ofstream outFile("rtn-output.csv");

    //per-routine info
    for (int i = 0; i < totalRoutinesFound; i++) {
        if (routinesArray[i].instructionCount > 0 || routinesArray[i].callCount > 0) {
            outFile << routinesArray[i].imageName << ", "
                    << "0x" << std::hex << routinesArray[i].imageAddress << std::dec << ", "
                    << routinesArray[i].routineName << ", "
                    << "0x" << std::hex << routinesArray[i].routineAddress << std::dec << ", "
                    << routinesArray[i].instructionCount << ", "
                    << routinesArray[i].callCount << std::endl;
        }
    }

    //per-register info
    for (int i = 0; i < NUM_TRACKED_REGS; i++) {
        const RegSamples& s = regSamples[i];
        outFile << TRACKED_REG_NAMES[i] << " values: ";
        for (int j = 0; j < s.count; j++) {
            outFile << "0x" << std::hex << s.values[j] << std::dec;
            if (j < s.count - 1) outFile << " -> ";
        }
        if (s.count >= 2) {
            UINT64 sumAbsDelta = 0;
            for (int j = 1; j < s.count; j++) {
                INT64 d = (INT64)(s.values[j] - s.values[j - 1]);
                sumAbsDelta += (d < 0) ? (UINT64)(-d) : (UINT64)d;
            }
            UINT64 avg = sumAbsDelta / (s.count - 1);
            outFile << "   Has an Average delta: Yes   Average delta: 0x"
                    << std::hex << avg << std::dec;
        } else {
            outFile << "   Has an Average delta: No";
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
