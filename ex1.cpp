#include "pin.H"
#include <iostream>
#include <fstream>
#include <string>

struct RoutineData {
    std::string imageName;
    ADDRINT imageAddress;
    std::string routineName;
    ADDRINT routineAddress;
    UINT64 instructionCount;
    UINT64 callCount;
};

RoutineData routinesArray[1000];
int totalRoutinesFound = 0;

void CountRoutineCall(UINT64* callCountPtr) {
    (*callCountPtr)++;
}

void CountInstructions(UINT64* instructionCountPtr, UINT32 numInstructionsInBlock) {
    (*instructionCountPtr) += numInstructionsInBlock;
}

bool CompareRoutines(const RoutineData& a, const RoutineData& b) {
    return a.instructionCount > b.instructionCount;
}

VOID Routine(RTN rtn, VOID* v) {
    if (totalRoutinesFound >= 1000) return;
    RoutineData* myData = &routinesArray[totalRoutinesFound];
    totalRoutinesFound++;
    
    myData->routineName = RTN_Name(rtn);
    myData->routineAddress = RTN_Address(rtn);

    SEC sec = RTN_Sec(rtn);
    if (SEC_Valid(sec)) {
        IMG img = SEC_Img(sec);
        if (IMG_Valid(img)) {
            myData->imageName = IMG_Name(img);
            myData->imageAddress = IMG_LowAddress(img);
        }
    }

    RTN_Open(rtn);
    RTN_InsertCall(rtn, IPOINT_BEFORE, (AFUNPTR)CountRoutineCall, IARG_PTR, &(myData->callCount), IARG_END);

    for (INS ins = RTN_InsHead(rtn); INS_Valid(ins); ins = INS_Next(ins)) {
        INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)CountInstructions, 
                       IARG_PTR, &(myData->instructionCount), 
                       IARG_UINT32, 1, 
                       IARG_END);
    }
    RTN_Close(rtn);
}

VOID FINI(INT32 code, VOID* v) {
    std::sort(routinesArray, routinesArray + totalRoutinesFound, CompareRoutines);
    std::ofstream outFile("rtn-output.csv");
    for (int i = 0; i < totalRoutinesFound; i++) {
        if (routinesArray[i].callCount > 0) {
            outFile << routinesArray[i].imageName << ", "
                    << "0x" << std::hex << routinesArray[i].imageAddress << std::dec << ", "
                    << routinesArray[i].routineName << ", "
                    << "0x" << std::hex << routinesArray[i].routineAddress << std::dec << ", "
                    << routinesArray[i].instructionCount << ", "
                    << routinesArray[i].callCount << std::endl;
        }
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
    PIN_AddFiniFunction(FINI, 0);
    PIN_StartProgram();
    return 0;
}