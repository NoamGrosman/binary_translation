#include "pin.H"
#include <iostream>
#include <fstream>
#include <string>
#include <algorithm>
#include <vector>
#include <map>
#include <unordered_map>

// ============================================================
// Part 1: per-BBL edge profiling
// ============================================================

struct BblData {
    ADDRINT address;
    UINT64 execCount;

    bool hasCondTail;
    UINT64 takenCount;
    UINT64 fallthruCount;

    bool hasIndirectTail;
    std::unordered_map<ADDRINT, UINT64> indirectTargets;

    BblData(ADDRINT a)
        : address(a), execCount(0),
          hasCondTail(false), takenCount(0), fallthruCount(0),
          hasIndirectTail(false) {}
};

static std::unordered_map<ADDRINT, BblData*> bblMap;

VOID BumpExec(UINT64* p) { (*p)++; }

VOID CondBranchCb(BblData* bd, BOOL taken) {
    if (taken) bd->takenCount++;
    else       bd->fallthruCount++;
}

VOID IndirectBranchCb(BblData* bd, ADDRINT target, BOOL taken) {
    if (taken) bd->indirectTargets[target]++;
}

// ============================================================
// Part 2: per-routine register tracking + RTN info
// ============================================================

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

static const int MAX_ROUTINES = 10000;
static RoutineData routinesArray[MAX_ROUTINES];
static int totalRoutinesFound = 0;
static std::unordered_map<ADDRINT, RoutineData*> rtnAddrToData;

VOID CountRoutineCall(UINT64* callCountPtr) { (*callCountPtr)++; }

VOID CountBblInstructions(UINT64* instructionCountPtr, UINT32 numIns) {
    (*instructionCountPtr) += numIns;
}

VOID RecordRegValue(RegSamples* s, ADDRINT val) {
    if (s->count < MAX_REG_SAMPLES) {
        s->values[s->count++] = val;
    }
}

// ============================================================
// Instrumentation
// ============================================================

VOID Routine(RTN rtn, VOID* v) {
    ADDRINT addr = RTN_Address(rtn);
    if (rtnAddrToData.find(addr) != rtnAddrToData.end()) return;
    if (totalRoutinesFound >= MAX_ROUTINES) return;

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
    RoutineData* rtnData = NULL;
    if (RTN_Valid(rtn)) {
        std::unordered_map<ADDRINT, RoutineData*>::iterator it =
            rtnAddrToData.find(RTN_Address(rtn));
        if (it != rtnAddrToData.end()) rtnData = it->second;
    }

    for (BBL bbl = TRACE_BblHead(trace); BBL_Valid(bbl); bbl = BBL_Next(bbl)) {
        ADDRINT bblAddr = BBL_Address(bbl);

        // Part 1: BBL exec count
        std::unordered_map<ADDRINT, BblData*>::iterator bit = bblMap.find(bblAddr);
        BblData* bd;
        if (bit == bblMap.end()) {
            bd = new BblData(bblAddr);
            bblMap[bblAddr] = bd;
        } else {
            bd = bit->second;
        }

        BBL_InsertCall(bbl, IPOINT_ANYWHERE, (AFUNPTR)BumpExec,
                       IARG_PTR, &(bd->execCount), IARG_END);

        // Per-routine instruction count
        if (rtnData) {
            BBL_InsertCall(bbl, IPOINT_ANYWHERE, (AFUNPTR)CountBblInstructions,
                           IARG_PTR, &(rtnData->instructionCount),
                           IARG_UINT32, BBL_NumIns(bbl),
                           IARG_END);
        }

        // Part 1: classify BBL terminator
        INS tail = BBL_InsTail(bbl);
        if (INS_IsBranch(tail) && INS_HasFallThrough(tail)) {
            bd->hasCondTail = true;
            INS_InsertCall(tail, IPOINT_BEFORE, (AFUNPTR)CondBranchCb,
                           IARG_PTR, bd, IARG_BRANCH_TAKEN, IARG_END);
        } else if (INS_IsIndirectControlFlow(tail)) {
            bd->hasIndirectTail = true;
            INS_InsertCall(tail, IPOINT_BEFORE, (AFUNPTR)IndirectBranchCb,
                           IARG_PTR, bd,
                           IARG_BRANCH_TARGET_ADDR, IARG_BRANCH_TAKEN,
                           IARG_END);
        }

        // Part 2: per-instruction register write tracking, scoped to this routine
        if (rtnData) {
            for (INS ins = BBL_InsHead(bbl); INS_Valid(ins); ins = INS_Next(ins)) {
                if (!INS_HasFallThrough(ins)) continue;
                for (int i = 0; i < NUM_TRACKED_REGS; i++) {
                    if (INS_RegWContain(ins, TRACKED_REGS[i])) {
                        INS_InsertCall(ins, IPOINT_AFTER, (AFUNPTR)RecordRegValue,
                                       IARG_PTR, &(rtnData->regs[i]),
                                       IARG_REG_VALUE, TRACKED_REGS[i],
                                       IARG_END);
                    }
                }
            }
        }
    }
}

// ============================================================
// Output
// ============================================================

bool CompareBblByExec(const BblData* a, const BblData* b) {
    return a->execCount > b->execCount;
}

bool CompareTargetByCount(const std::pair<ADDRINT, UINT64>& a,
                          const std::pair<ADDRINT, UINT64>& b) {
    return a.second > b.second;
}

bool CompareRoutinesByInst(const RoutineData& a, const RoutineData& b) {
    return a.instructionCount > b.instructionCount;
}

VOID WriteEdgeProfile() {
    std::vector<BblData*> sorted;
    sorted.reserve(bblMap.size());
    for (std::unordered_map<ADDRINT, BblData*>::iterator it = bblMap.begin();
         it != bblMap.end(); ++it) {
        if (it->second->execCount > 0) sorted.push_back(it->second);
    }
    std::sort(sorted.begin(), sorted.end(), CompareBblByExec);

    std::ofstream out("edge-profile.csv");
    for (size_t i = 0; i < sorted.size(); i++) {
        BblData* bd = sorted[i];
        out << "0x" << std::hex << bd->address << std::dec
            << ", " << bd->execCount;

        if (bd->hasCondTail) {
            out << ", " << bd->takenCount << ", " << bd->fallthruCount;
        }

        if (bd->hasIndirectTail && !bd->indirectTargets.empty()) {
            std::vector<std::pair<ADDRINT, UINT64> > targets(
                bd->indirectTargets.begin(), bd->indirectTargets.end());
            std::sort(targets.begin(), targets.end(), CompareTargetByCount);
            size_t lim = targets.size() < 10 ? targets.size() : 10;
            for (size_t k = 0; k < lim; k++) {
                out << ", 0x" << std::hex << targets[k].first << std::dec
                    << ", " << targets[k].second;
            }
        }
        out << std::endl;
    }
    out.close();
}

VOID WriteRtnOutput() {
    std::sort(routinesArray, routinesArray + totalRoutinesFound,
              CompareRoutinesByInst);

    std::ofstream out("rtn-output.csv");
    for (int i = 0; i < totalRoutinesFound; i++) {
        RoutineData& r = routinesArray[i];
        if (r.instructionCount == 0 && r.callCount == 0) continue;

        out << r.imageName << ", "
            << "0x" << std::hex << r.imageAddress << std::dec << ", "
            << r.routineName << ", "
            << "0x" << std::hex << r.routineAddress << std::dec << ", "
            << r.instructionCount << ", "
            << r.callCount << std::endl;

        for (int j = 0; j < NUM_TRACKED_REGS; j++) {
            RegSamples& s = r.regs[j];
            out << TRACKED_REG_NAMES[j] << " values: ";
            for (int k = 0; k < s.count; k++) {
                out << "0x" << std::hex << s.values[k] << std::dec;
                if (k < s.count - 1) out << " -> ";
            }
            if (s.count >= 2) {
                UINT64 sumAbs = 0;
                for (int k = 1; k < s.count; k++) {
                    INT64 d = (INT64)(s.values[k] - s.values[k - 1]);
                    sumAbs += (d < 0) ? (UINT64)(-d) : (UINT64)d;
                }
                UINT64 avg = sumAbs / (s.count - 1);
                out << "   Has an Average delta: Yes   Average delta: 0x"
                    << std::hex << avg << std::dec;
            } else {
                out << "   Has an Average delta: No";
            }
            out << std::endl;
        }
    }
    out.close();
}

VOID Fini(INT32 code, VOID* v) {
    WriteEdgeProfile();
    WriteRtnOutput();
}

int main(int argc, char* argv[]) {
    PIN_InitSymbols();
    if (PIN_Init(argc, argv)) {
        std::cerr << "Pin failed to initialize." << std::endl;
        return 1;
    }
    RTN_AddInstrumentFunction(Routine, 0);
    TRACE_AddInstrumentFunction(Trace, 0);
    PIN_AddFiniFunction(Fini, 0);
    PIN_StartProgram();
    return 0;
}
