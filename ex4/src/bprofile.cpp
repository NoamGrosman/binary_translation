/*########################################################################################################*/
// cd /nfs/iil/ptl/bt/ghaber1/pin/pin-2.10-45467-gcc.3.4.6-ia32_intel64-linux/source/tools/SimpleExamples
// make btranslate.test
//  ../../../pin -t obj-intel64/btranslate.so -- ~/workdir/tst
/*########################################################################################################*/
/*BEGIN_LEGAL
Intel Open Source License

Copyright (c) 2002-2011 Intel Corporation. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:

Redistributions of source code must retain the above copyright notice,
this list of conditions and the following disclaimer.  Redistributions
in binary form must reproduce the above copyright notice, this list of
conditions and the following disclaimer in the documentation and/or
other materials provided with the distribution.  Neither the name of
the Intel Corporation nor the names of its contributors may be used to
endorse or promote products derived from this software without
specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE INTEL OR
ITS CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
END_LEGAL */
/* ===================================================================== */

/* ===================================================================== */
/*! @file
 * This probe pintool generates translated code of all the routines, places them 
 * in an allocated Translation Cache (TC) along with instrumentation instructions that collect 
 * profiling for each BBL and for each indirect jump target.
 *
 * The profiling data is then printed on exit into the output file bprofile.out.
 */

#include "pin.H"
extern "C" {
#include "xed-interface.h"
}
#include <iostream>
#include <iomanip>
#include <fstream>
#include <sys/mman.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <malloc.h>
#include <errno.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <values.h>
#include <set>
#include <map>
#include <time.h>
#include <fstream>
#include <vector>
#include <string>
#include <sstream>
#include <algorithm>
#include <signal.h>
#include <ucontext.h>

using namespace std;

/*======================================================================*/
/* commandline switches                                                 */
/*======================================================================*/
KNOB<BOOL>   KnobVerbose(KNOB_MODE_WRITEONCE,    "pintool",
    "verbose", "0", "Verbose run");

KNOB<BOOL>   KnobDumpOrigCode(KNOB_MODE_WRITEONCE,    "pintool",
    "dump_orig_code", "0", "Dump Original non-translated Code");

KNOB<BOOL>   KnobDumpTranslatedCode(KNOB_MODE_WRITEONCE,    "pintool",
    "dump_tc", "0", "Dump Translated Code");

KNOB<BOOL>   KnobDoNotCommitTranslatedCode(KNOB_MODE_WRITEONCE,    "pintool",
    "no_tc_commit", "0", "Do not commit translated code");

KNOB<UINT> KnobNumSecsDuringProfile(KNOB_MODE_WRITEONCE,    "pintool",
    "prof_time", "2", "Number of seconds for collecting BBL counters");

KNOB<BOOL> KnobDumpProfile(KNOB_MODE_WRITEONCE,    "pintool",
    "dump_prof", "0", "Dump profiling information");

KNOB<BOOL> KnobNoProfile(KNOB_MODE_WRITEONCE,    "pintool",
    "no_prof", "0", "Do not collect profile information");

// Debug knobs to isolate the Task-2 sub-optimizations.
KNOB<BOOL> KnobNoMemAdd(KNOB_MODE_WRITEONCE,    "pintool",
    "no_mem_add", "0", "Disable the ADD [counter],1 flags-dead optimization");

KNOB<BOOL> KnobNoSkipDead(KNOB_MODE_WRITEONCE,    "pintool",
    "no_skip_dead", "0", "Disable skipping save/restore of dead registers");

KNOB<UINT64> KnobDumpAround(KNOB_MODE_WRITEONCE,    "pintool",
    "dump_around", "0", "Dump instr map entries around this TC address");

// Bisection knobs: apply the dead-register masks only to stubs whose
// ordinal (emission order) lies in [skip_lo, skip_hi).
KNOB<UINT64> KnobSkipLo(KNOB_MODE_WRITEONCE,    "pintool",
    "skip_lo", "0", "First stub ordinal to optimize");
KNOB<UINT64> KnobSkipHi(KNOB_MODE_WRITEONCE,    "pintool",
    "skip_hi", "0xffffffff", "One past last stub ordinal to optimize");

KNOB<UINT64> KnobReportStub(KNOB_MODE_WRITEONCE,    "pintool",
    "report_stub", "0xffffffffffffffff", "Print details of this stub ordinal");

KNOB<BOOL> KnobPoisonRax(KNOB_MODE_WRITEONCE,    "pintool",
    "poison_rax", "0", "Poison RAX with 0x4242.. in stubs that skip its save");

KNOB<BOOL> KnobCatchSegv(KNOB_MODE_WRITEONCE,    "pintool",
    "catch_segv", "0", "Install a SIGSEGV handler that dumps registers");

// Discriminator: keep the save/restore instructions (same layout as the
// unoptimized stub) but restore RAX from a garbage slot, reproducing the
// optimized stub's semantics (RAX clobbered) without any size change.
KNOB<BOOL> KnobDummyClobber(KNOB_MODE_WRITEONCE,    "pintool",
    "dummy_clobber", "0", "Same-size stub that clobbers RAX (debug)");

KNOB<BOOL> KnobHeapBbl(KNOB_MODE_WRITEONCE,    "pintool",
    "heap_bbl", "0", "Allocate bbl_map on the heap as originally (debug)");

// Debug: dump full register state on SIGSEGV (probe mode shares the
// process with the app, so a plain signal handler sees the app's state).
static void segv_report_handler(int sig, siginfo_t *si, void *uc_)
{
    ucontext_t *uc = (ucontext_t *)uc_;
    greg_t *g = uc->uc_mcontext.gregs;
    // glibc x86_64 greg indices (avoid the REG_* names clashing with Pin's).
    enum { MC_R8=0, MC_R9, MC_R10, MC_R11, MC_R12, MC_R13, MC_R14, MC_R15,
           MC_RDI, MC_RSI, MC_RBP, MC_RBX, MC_RDX, MC_RAX, MC_RCX, MC_RSP,
           MC_RIP, MC_EFL };
    char buf[1024];
    int n = snprintf(buf, sizeof(buf),
        "SEGVREPORT sig=%d addr=%p\n"
        " rip=%llx efl=%llx\n"
        " rax=%llx rbx=%llx rcx=%llx rdx=%llx\n"
        " rsi=%llx rdi=%llx rbp=%llx rsp=%llx\n"
        " r8=%llx r9=%llx r10=%llx r11=%llx\n"
        " r12=%llx r13=%llx r14=%llx r15=%llx\n",
        sig, si->si_addr,
        (unsigned long long)g[MC_RIP], (unsigned long long)g[MC_EFL],
        (unsigned long long)g[MC_RAX], (unsigned long long)g[MC_RBX],
        (unsigned long long)g[MC_RCX], (unsigned long long)g[MC_RDX],
        (unsigned long long)g[MC_RSI], (unsigned long long)g[MC_RDI],
        (unsigned long long)g[MC_RBP], (unsigned long long)g[MC_RSP],
        (unsigned long long)g[MC_R8],  (unsigned long long)g[MC_R9],
        (unsigned long long)g[MC_R10], (unsigned long long)g[MC_R11],
        (unsigned long long)g[MC_R12], (unsigned long long)g[MC_R13],
        (unsigned long long)g[MC_R14], (unsigned long long)g[MC_R15]);
    write(2, buf, n);
    // Also dump the top of the stack (possible return addresses).
    unsigned long long *sp = (unsigned long long *)g[MC_RSP];
    for (int i = 0; i < 8; i++) {
        n = snprintf(buf, sizeof(buf), " [rsp+%2d]=%llx\n", i*8, sp[i]);
        write(2, buf, n);
    }
    _exit(66);
}


/* ===================================================================== */
/* Global Variables */
/* ===================================================================== */
std::ofstream* out = 0;

// For XED:
#if defined(TARGET_IA32E)
    xed_state_t dstate = {XED_MACHINE_MODE_LONG_64, XED_ADDRESS_WIDTH_64b};
#else
    xed_state_t dstate = { XED_MACHINE_MODE_LEGACY_32, XED_ADDRESS_WIDTH_32b};
#endif

//For XED: Pass in the proper length: 15 is the max. But if you do not want to
//cross pages, you can pass less than 15 bytes, of course, the
//instruction might not decode if not enough bytes are provided.
const unsigned max_inst_len = XED_MAX_INSTRUCTION_BYTES;

ADDRINT lowest_sec_addr = 0;
ADDRINT highest_sec_addr = 0;

// tc containing the new code:
char *tc = nullptr;
unsigned tc_size = 0;
unsigned max_tc_size = 0;


// Array of original target addresses that cannot be translated in the TC.
ADDRINT *jump_to_orig_addr_map = nullptr;
unsigned jump_to_orig_addr_num = 0;

// basic instruction types.
typedef enum {
    RegularIns = 0,
    RtnHeadIns,
    ProfilingIns,

} ins_enum_t;

// instructions map with an entry for each new instruction in the code.
typedef struct {
    ADDRINT orig_ins_addr;
    ADDRINT new_ins_addr;
    ADDRINT orig_targ_addr;
    ADDRINT orig_rip_addr;
    ins_enum_t ins_type;
    char encoded_ins[XED_MAX_INSTRUCTION_BYTES];
    unsigned size;
    int targ_map_entry;
    unsigned bbl_num;
    xed_category_enum_t xed_category;
} instr_map_t;


// Instrs map:
instr_map_t *instr_map = NULL;
unsigned num_of_instr_map_entries = 0;
unsigned max_ins_count = 0;

#define MAX_TARG_ADDRS 0x3

// Bbl map of all the bbl exec counters to be collected at runtime:
typedef struct {
  UINT64 counter;
  UINT64 fallthru_counter; // for BBLs that terminate with a cond branch.
  ADDRINT targ_addr[MAX_TARG_ADDRS+1];
  UINT64  targ_count[MAX_TARG_ADDRS+1];
  unsigned starting_ins_entry;
  unsigned terminating_ins_entry;
} bbl_map_t;

bbl_map_t *bbl_map;
unsigned bbl_num = 0;
unsigned max_bbl_count = 0;
std::map<ADDRINT, unsigned> entry_map;

unsigned max_rtn_count = 0;

struct timespec start_running_time;
struct timespec end_running_time;

/* ============================================================= */
/* Service instr routines                                        */
/* ============================================================= */
bool isUncondJump(INS ins)
{
    const xed_decoded_inst_t* xedd = INS_XedDec(ins);
    xed_category_enum_t category_enum = xed_decoded_inst_get_category(xedd);
    if (category_enum == XED_CATEGORY_UNCOND_BR)
      return true;
    return false;
}

bool isJumpOrRet(INS ins)
{
   if (!INS_IsCall(ins) &&
       (INS_IsIndirectControlFlow(ins) ||
        INS_IsDirectControlFlow(ins) ||
        INS_IsRet(ins)))
     return true;

   return false;
}

bool isBackwardJump(INS ins)
{
  return (!INS_IsCall(ins) && INS_IsDirectControlFlow(ins) &&
          INS_DirectControlFlowTargetAddress(ins) < INS_Address(ins));
}

int create_nop7_xedd_instr(xed_decoded_inst_t *xedd)
{
  xed_encoder_instruction_t enc_instr;
  xed_encoder_request_t enc_req;
  char encoded_ins[XED_MAX_INSTRUCTION_BYTES];
  unsigned int ilen = XED_MAX_INSTRUCTION_BYTES;
  unsigned int olen = 0;
  
  xed_inst0(&enc_instr, dstate, XED_ICLASS_NOP7, 64);
  
  xed_encoder_request_zero_set_mode(&enc_req, &dstate);
  xed_bool_t convert_ok = xed_convert_to_encoder_request(&enc_req, &enc_instr);
  if (!convert_ok) {
      cerr << "conversion to encode request failed" << endl;
      return -1;
  }
  xed_error_enum_t xed_error = xed_encode(&enc_req,
            reinterpret_cast<UINT8*>(encoded_ins), ilen, &olen);
  if (xed_error != XED_ERROR_NONE) {
      cerr << "ENCODE ERROR: " << xed_error_enum_t2str(xed_error) << endl;
    return -1;
  }
  xed_decoded_inst_zero_set_mode(xedd, &dstate);
  xed_error_enum_t xed_code = xed_decode(xedd, reinterpret_cast<UINT8*>(&encoded_ins), max_inst_len); // xed_decode(&xedd, nop7, max_inst_len);
  if (xed_code != XED_ERROR_NONE) {
      cerr << "DECODE ERROR: " << xed_error_enum_t2str(xed_code) << endl;
      return -1;;
  }
  return 0;
}


/* ============================================================= */
/* Service dump routines                                         */
/* ============================================================= */

/*********************/
/* dump_image_instrs */
/*********************/
void dump_image_instrs(IMG img)
{
    for (SEC sec = IMG_SecHead(img); SEC_Valid(sec); sec = SEC_Next(sec))
    {
        for (RTN rtn = SEC_RtnHead(sec); RTN_Valid(rtn); rtn = RTN_Next(rtn))
        {

            // Open the RTN.
            RTN_Open( rtn );

            cerr << RTN_Name(rtn) << ":" << endl;

            for( INS ins = RTN_InsHead(rtn); INS_Valid(ins); ins = INS_Next(ins) )
            {
                  cerr << "0x" << hex << INS_Address(ins) << ": " << INS_Disassemble(ins) << endl;
            }

            // Close the RTN.
            RTN_Close( rtn );

            cerr << endl;
        }
    }
}


/*************************/
/* dump_instr_from_xedd */
/*************************/
void dump_instr_from_xedd (xed_decoded_inst_t* xedd, ADDRINT address)
{
    // debug print decoded instr:
    char disasm_buf[2048];

    xed_uint64_t runtime_address = static_cast<UINT64>(address);  // set the runtime adddress for disassembly

    xed_format_context(XED_SYNTAX_INTEL, xedd, disasm_buf, sizeof(disasm_buf), static_cast<UINT64>(runtime_address), 0, 0);

    cerr << hex << address << ": " << disasm_buf <<  endl;
}


/************************/
/* dump_instr_from_mem */
/************************/
void dump_instr_from_mem (ADDRINT *address, ADDRINT new_addr)
{
  char disasm_buf[2048];
  xed_decoded_inst_t new_xedd;

  xed_decoded_inst_zero_set_mode(&new_xedd,&dstate);

  xed_error_enum_t xed_code = xed_decode(&new_xedd, reinterpret_cast<UINT8*>(address), max_inst_len);

  BOOL xed_ok = (xed_code == XED_ERROR_NONE);
  if (!xed_ok){
      cerr << "invalid opcode" << endl;
  }

  xed_format_context(XED_SYNTAX_INTEL, &new_xedd, disasm_buf, 2048, static_cast<UINT64>(new_addr), 0, 0);

  cerr << "0x" << hex << new_addr << ": " << disasm_buf <<  endl;

}


/****************************/
/*  dump_entire_instr_map() */
/****************************/
void dump_entire_instr_map()
{
    for (unsigned i=0; i < num_of_instr_map_entries; i++) {
      // Print the routine name if known.
      if (instr_map[i].ins_type == RtnHeadIns) {
        PIN_LockClient();
        RTN rtn = RTN_FindByAddress(instr_map[i].orig_ins_addr);
        if (rtn == RTN_Invalid()) {
            cerr << "Unknown"  << ":" << endl;
        } else {
            cerr << RTN_Name(rtn) << ":" << endl;
        }
        PIN_UnlockClient();
      }

      if (!instr_map[i].size)
        continue;


      dump_instr_from_mem ((ADDRINT *)instr_map[i].encoded_ins, instr_map[i].orig_ins_addr);
    }
}

/*******************/
/*  dump_profile() */
/*******************/
// One output line of edge-profile.csv (exercise-4 PDF format; taken and
// fallthru are emitted on EVERY line, derived from the terminator type):
//   <bbl_addr>, <exec_count>, <taken>, <fallthru>[, <t_addr>, <t_cnt> ...]
typedef struct {
    ADDRINT addr;
    UINT64 count;
    std::string tail;
} csv_line_t;

static bool csv_line_hotter(const csv_line_t &a, const csv_line_t &b)
{
    return a.count > b.count;
}

void dump_profile()
{
    std::vector<csv_line_t> lines;

    for (unsigned b = 0; b < bbl_num; b++) {
      if (!bbl_map[b].counter)
        continue;

      // The BBL address is the orig address of its first real instruction;
      // skip profiling-stub entries (e.g. the fall-through stub of the
      // previous BBL) that share the entry range.
      unsigned e = bbl_map[b].starting_ins_entry;
      while (e < num_of_instr_map_entries && instr_map[e].ins_type == ProfilingIns)
        e++;
      if (e >= num_of_instr_map_entries)
        continue;

      csv_line_t ln;
      ln.addr = instr_map[e].orig_ins_addr;
      ln.count = bbl_map[b].counter;

      std::ostringstream os;
      unsigned t = bbl_map[b].terminating_ins_entry;
      xed_category_enum_t term_cat = (t < num_of_instr_map_entries) ?
          instr_map[t].xed_category : XED_CATEGORY_INVALID;

      // Derive taken/fallthru from the terminator type:
      // - cond branch: fallthru is the collected counter, taken the rest;
      // - uncond jump (direct or indirect) or ret: always taken;
      // - anything else (call, or the BBL ends only because the next
      //   instruction is a jump target): always falls through.
      UINT64 taken = 0, ft = 0;
      if (term_cat == XED_CATEGORY_COND_BR) {
        ft = bbl_map[b].fallthru_counter;
        taken = (ln.count >= ft) ? (ln.count - ft) : 0;
      } else if (term_cat == XED_CATEGORY_UNCOND_BR ||
                 term_cat == XED_CATEGORY_RET) {
        taken = ln.count;
      } else {
        ft = ln.count;
      }
      os << ", " << dec << taken << ", " << ft;

      // For BBLs terminating with an indirect jump: emit non-empty target
      // buckets, sorted by count descending.
      std::vector<std::pair<UINT64, ADDRINT> > tg;
      for (unsigned j = 0; j <= MAX_TARG_ADDRS; j++) {
        if (bbl_map[b].targ_count[j])
          tg.push_back(std::make_pair(bbl_map[b].targ_count[j],
                                      bbl_map[b].targ_addr[j]));
      }
      std::sort(tg.rbegin(), tg.rend());
      for (unsigned j = 0; j < tg.size(); j++)
        os << ", 0x" << hex << tg[j].second << ", " << dec << tg[j].first;
      ln.tail = os.str();
      lines.push_back(ln);
    }

    // Hottest BBLs first.
    std::stable_sort(lines.begin(), lines.end(), csv_line_hotter);

    for (unsigned i = 0; i < lines.size(); i++)
      *out << "0x" << hex << lines[i].addr << ", " << dec << lines[i].count
           << lines[i].tail << "\n";
    out->flush();
}

/**************************/
/* dump_instr_map_entry() */
/**************************/
void dump_instr_map_entry(unsigned instr_map_entry)
{
    cerr << dec << instr_map_entry << ": ";
    cerr << " orig_ins_addr: 0x" << hex << instr_map[instr_map_entry].orig_ins_addr;
    cerr << " new_ins_addr: 0x" << hex << instr_map[instr_map_entry].new_ins_addr;

    if (instr_map[instr_map_entry].orig_targ_addr) {
      cerr << " orig_targ_addr: 0x" << hex << instr_map[instr_map_entry].orig_targ_addr;
      ADDRINT new_targ_addr;
      if (instr_map[instr_map_entry].targ_map_entry >= 0)
          new_targ_addr = instr_map[instr_map[instr_map_entry].targ_map_entry].new_ins_addr;
      else
          new_targ_addr = instr_map[instr_map_entry].orig_targ_addr;
      cerr << " new_targ_addr: 0x" << hex << new_targ_addr;
    }

    cerr << "    new instr:";
    dump_instr_from_mem((ADDRINT *)instr_map[instr_map_entry].encoded_ins,
                        instr_map[instr_map_entry].new_ins_addr);
}


/*************/
/* dump_tc() */
/*************/
void dump_tc(char *tc, unsigned size_tc)
{
  char disasm_buf[2048];
  xed_decoded_inst_t new_xedd;
  ADDRINT address = (ADDRINT)&tc[0];

  while (address < (ADDRINT)&tc[size_tc]) {

      xed_decoded_inst_zero_set_mode(&new_xedd,&dstate);
      xed_error_enum_t xed_code = xed_decode(&new_xedd, reinterpret_cast<UINT8*>(address), max_inst_len);

      BOOL xed_ok = (xed_code == XED_ERROR_NONE);
      if (!xed_ok){
          cerr << "invalid opcode" << endl;
          return;
      }

      xed_format_context(XED_SYNTAX_INTEL, &new_xedd, disasm_buf, 2048, static_cast<UINT64>(address), 0, 0);

      cerr << "0x" << hex << address << ": " << disasm_buf <<  endl;

      address += xed_decoded_inst_get_length (&new_xedd);
  }
}


/* ============================================================= */
/* Translation routines                                         */
/* ============================================================= */


/***************************/
/* disable_profiling_in_tc */
/***************************/
// Patching live code while the application executes it is a race (a
// half-written 5-byte jmp decodes as a jump with a garbage displacement),
// so the patches are only PREPARED here, on the timer thread, and then
// APPLIED inside a signal handler that runs on the (single) application
// thread itself - which by construction cannot be executing a stub while
// it is inside the handler.
typedef struct {
    char *dst;
    unsigned char bytes[8];
    unsigned len;
} code_patch_t;

static code_patch_t *pending_patches = NULL;
static unsigned num_pending_patches = 0;
static volatile sig_atomic_t patches_applied = 0;

static void apply_patches_handler(int sig)
{
    for (unsigned i = 0; i < num_pending_patches; i++)
        memcpy(pending_patches[i].dst, pending_patches[i].bytes,
               pending_patches[i].len);
    patches_applied = 1;
}

int disable_profiling_in_tc(instr_map_t * instr_map, unsigned num_of_instr_map_entries)
{
    // Allocate the patch list (one entry per stub-head NOP).
    unsigned num_stub_heads = 0;
    for (unsigned i = 0; i < num_of_instr_map_entries; i++)
        if (instr_map[i].ins_type == ProfilingIns &&
            instr_map[i].xed_category == XED_CATEGORY_WIDENOP)
            num_stub_heads++;
    pending_patches = (code_patch_t *)malloc(
        (size_t)(num_stub_heads + 1) * sizeof(code_patch_t));
    if (!pending_patches) {
        cerr << "failed to allocate patch list\n";
        return -1;
    }
    num_pending_patches = 0;

    for (unsigned i = 0; i < num_of_instr_map_entries; i++) {
        // Check for the case of a NOP instr at the head of a
        // pofiling code stub and replace it by a jump instr that skips it.
        if (instr_map[i].ins_type == ProfilingIns &&
            instr_map[i].xed_category == XED_CATEGORY_WIDENOP) {
            // Calculate the jump displacement.
            unsigned j = 1;
            xed_int64_t disp = 0;
            while (instr_map[i+j].ins_type == ProfilingIns) {
                disp += instr_map[i+j].size;
                j++;
            }

          xed_encoder_instruction_t enc_instr;
          xed_encoder_request_t enc_req;
          unsigned int ilen = XED_MAX_INSTRUCTION_BYTES;
          char encoded_jmp_ins[XED_MAX_INSTRUCTION_BYTES];
          unsigned int olen = 5; // skip jump instr is exactly 5 bytes long.
          
          disp += (instr_map[i].size - olen);
          xed_inst1(&enc_instr, dstate,  XED_ICLASS_JMP, 64, xed_relbr(disp, 32));
          
          xed_encoder_request_zero_set_mode(&enc_req, &dstate);
          xed_bool_t convert_ok = xed_convert_to_encoder_request(&enc_req, &enc_instr);
          if (!convert_ok) {
              cerr << "conversion to encode request failed" << endl;
              return -1;
          }           
          xed_error_enum_t xed_error = xed_encode(&enc_req,
                    reinterpret_cast<UINT8*>(encoded_jmp_ins), ilen, &olen);
          if (xed_error != XED_ERROR_NONE) {
              cerr << "ENCODE ERROR: " << xed_error_enum_t2str(xed_error) << endl;
            return -1;
          }

          if (olen > instr_map[i].size) {
             cerr << " unable to set a relative jump to skip the profiling code stub at: "
                  << hex << "0x" << instr_map[i].new_ins_addr << "\n";
             return -1;
          }

          // Queue the bypassing jump instr; it is written over the NOP
          // later, on the application thread (see apply_patches_handler).
          pending_patches[num_pending_patches].dst = (char *)instr_map[i].new_ins_addr;
          memcpy(pending_patches[num_pending_patches].bytes, encoded_jmp_ins, olen);
          pending_patches[num_pending_patches].len = olen;
          num_pending_patches++;
          i += (j - 1);
       }
    }
    return 0;
}

/*************************/
/* add_new_instr_entry() */
/*************************/
int add_new_instr_entry(xed_decoded_inst_t *xedd, ADDRINT pc, ins_enum_t ins_type)
{
    // copy target addr to instr map:
    ADDRINT orig_targ_addr = 0x0;

    // Check if the instruction has a branch displacement:
    xed_uint_t disp_byts = xed_decoded_inst_get_branch_displacement_width(xedd);
    xed_int32_t disp;
    if (disp_byts > 0) { // there is a branch offset.
      disp = xed_decoded_inst_get_branch_displacement(xedd);
      orig_targ_addr = pc + xed_decoded_inst_get_length (xedd) + disp;
    }

    // copy rip-relative addr to instr map:
    ADDRINT orig_rip_addr = 0x0;

    // check for a rip-relative displacement:
    unsigned memops = xed_decoded_inst_number_of_memory_operands(xedd);
    if (memops) {
      xed_reg_enum_t base_reg = xed_decoded_inst_get_base_reg(xedd, 0);
      if (base_reg == XED_REG_RIP) {
         unsigned size = xed_decoded_inst_get_length (xedd);
         xed_int64_t disp = xed_decoded_inst_get_memory_displacement(xedd, 0);
         orig_rip_addr = (ADDRINT)(pc + disp + size);
      }
    }

    // Converts the decoder request to a valid encoder request:
    xed_encoder_request_init_from_decode (xedd);

    unsigned new_size = 0;

    xed_error_enum_t xed_error =
       xed_encode (xedd, reinterpret_cast<UINT8*>(instr_map[num_of_instr_map_entries].encoded_ins),
                   max_inst_len , &new_size);
    if (xed_error != XED_ERROR_NONE) {
        cerr << "ENCODE ERROR: " << xed_error_enum_t2str(xed_error) << endl;
        return -1;
    }

    // Add a new entry to instr_map:
    //
    instr_map[num_of_instr_map_entries].orig_ins_addr = pc;
    instr_map[num_of_instr_map_entries].new_ins_addr = 0x0;
    instr_map[num_of_instr_map_entries].orig_targ_addr = orig_targ_addr;
    instr_map[num_of_instr_map_entries].orig_rip_addr = orig_rip_addr;
    instr_map[num_of_instr_map_entries].targ_map_entry = -1;
    instr_map[num_of_instr_map_entries].size = new_size;
    instr_map[num_of_instr_map_entries].ins_type = ins_type;
    instr_map[num_of_instr_map_entries].bbl_num = bbl_num;
    instr_map[num_of_instr_map_entries].xed_category = xed_decoded_inst_get_category(xedd);

    num_of_instr_map_entries++;

    if (num_of_instr_map_entries >= max_ins_count) {
        cerr << "out of memory for map_instr" << endl;
        return -1;
    }

    // debug print new encoded instr:
    if (KnobVerbose) {
        cerr << "    new instr:";
        dump_instr_from_mem((ADDRINT *)instr_map[num_of_instr_map_entries-1].encoded_ins,
                            instr_map[num_of_instr_map_entries-1].new_ins_addr);
    }

    return new_size;
}

/***************************/
/* add_new_encoded_instr() */
/***************************/
int add_new_encoded_instr(ADDRINT ins_addr, xed_encoder_instruction_t *enc_instr, ins_enum_t ins_type) {
    char encoded_ins[XED_MAX_INSTRUCTION_BYTES];
    unsigned int ilen = XED_MAX_INSTRUCTION_BYTES;
    unsigned int olen = 0;
  
    // Convert the encoding instr to a valid encoder request.
    xed_encoder_request_t enc_req;    
    xed_encoder_request_zero_set_mode(&enc_req, &dstate);
    xed_bool_t convert_ok = xed_convert_to_encoder_request(&enc_req, enc_instr);
    if (!convert_ok) {
        cerr << "conversion to encode request failed" << endl;
        return -1;
    }
    
    // Encode instr.
    xed_error_enum_t xed_error = xed_encode(&enc_req,
              reinterpret_cast<UINT8*>(encoded_ins), ilen, &olen);
    if (xed_error != XED_ERROR_NONE) {
        cerr << "ENCODE ERROR: " << xed_error_enum_t2str(xed_error) << endl;
      return -1;
    }
  
    // Decode instr.
    xed_decoded_inst_t xedd;
    xed_decoded_inst_zero_set_mode(&xedd,&dstate);
    xed_error_enum_t xed_code = xed_decode(&xedd, reinterpret_cast<UINT8*>(&encoded_ins), max_inst_len);
    if (xed_code != XED_ERROR_NONE) {
        cerr << "ERROR: xed decode failed for instr at: " << "0x" << hex << ins_addr << endl;
        return -1;;
    }
    int rc = add_new_instr_entry(&xedd, ins_addr, ins_type);
    if (rc < 0) {
      cerr << "ERROR: failed during instructon translation." << endl;
      return -1;
    }
    return 0;
}

/* ============================================================= */
/* Task 2: dead-register analysis                                */
/*                                                               */
/* Before a profiling stub is emitted we compute which of        */
/* RAX / RBX / RCX / status-flags are DEAD at that point (their  */
/* value is overwritten on every path before being read again).  */
/* Dead registers do not need the stub's save/restore, and dead  */
/* flags allow replacing the whole RAX-based counter increment   */
/* with a single flag-clobbering ADD qword ptr [counter], 1.     */
/* The analysis is a standard backward liveness fixpoint over    */
/* each routine's instructions, conservative at every boundary   */
/* it cannot see across.                                         */
/* ============================================================= */

enum {
  LV_RAX   = 1,
  LV_RBX   = 2,
  LV_RCX   = 4,
  LV_FLAGS = 8,   // the 6 status flags (CF PF AF ZF SF OF) as one unit
  LV_ALL   = 0xF
};

typedef struct {
    ADDRINT addr;
    unsigned reads;        // LV_* bits read by the instruction
    unsigned kills;        // LV_* bits fully (unconditionally) overwritten
    ADDRINT direct_targ;   // direct branch/jump target, 0 if none
    bool is_cond_br;
    bool is_direct_jmp;    // unconditional direct jump
    bool is_indirect_jmp;  // indirect jump that is not a call/ret
    bool is_ret;
} live_ins_info_t;

static std::vector<live_ins_info_t> rtn_ins_info;
static std::map<ADDRINT, unsigned> rtn_ins_index;
static std::vector<unsigned> rtn_live_in;
// live-in mask per original instruction address, for the routine
// currently being translated.
static std::map<ADDRINT, unsigned> live_in_at_addr;

/*******************************/
/* collect_ins_liveness_info() */
/*******************************/
static void collect_ins_liveness_info(INS ins)
{
    live_ins_info_t inf;
    memset(&inf, 0, sizeof(inf));
    inf.addr = INS_Address(ins);

    bool predicated = INS_IsPredicated(ins); // CMOVcc, REP...: writes may not happen

    for (UINT32 k = 0; k < INS_MaxNumRRegs(ins); k++) {
        REG r = REG_FullRegName(INS_RegR(ins, k));
        if (r == LEVEL_BASE::REG_RAX) inf.reads |= LV_RAX;
        else if (r == LEVEL_BASE::REG_RBX) inf.reads |= LV_RBX;
        else if (r == LEVEL_BASE::REG_RCX) inf.reads |= LV_RCX;
    }
    for (UINT32 k = 0; k < INS_MaxNumWRegs(ins); k++) {
        REG w = INS_RegW(ins, k);
        // Only 64-bit and 32-bit (zero-extending) writes kill the full
        // register; 16/8-bit writes preserve the upper bits.
        unsigned bit =
            (w == LEVEL_BASE::REG_RAX || w == LEVEL_BASE::REG_EAX) ? LV_RAX :
            (w == LEVEL_BASE::REG_RBX || w == LEVEL_BASE::REG_EBX) ? LV_RBX :
            (w == LEVEL_BASE::REG_RCX || w == LEVEL_BASE::REG_ECX) ? LV_RCX : 0;
        if (bit && !predicated)
            inf.kills |= bit;
    }

    // Status flags, precisely via XED (which flags are read; killed only
    // when ALL six status flags are unconditionally written/undefined).
    const xed_decoded_inst_t *xedd = INS_XedDec(ins);
    if (xed_decoded_inst_uses_rflags(xedd)) {
        const xed_simple_flag_t *fi = xed_decoded_inst_get_rflags_info(xedd);
        if (fi) {
            const xed_flag_set_t *rs = xed_simple_flag_get_read_flag_set(fi);
            const xed_flag_set_t *ws = xed_simple_flag_get_written_flag_set(fi);
            const xed_flag_set_t *us = xed_simple_flag_get_undefined_flag_set(fi);
            if (rs->s.cf || rs->s.pf || rs->s.af || rs->s.zf || rs->s.sf || rs->s.of)
                inf.reads |= LV_FLAGS;
            bool all_status =
                (ws->s.cf || us->s.cf) && (ws->s.pf || us->s.pf) &&
                (ws->s.af || us->s.af) && (ws->s.zf || us->s.zf) &&
                (ws->s.sf || us->s.sf) && (ws->s.of || us->s.of);
            if (all_status && xed_simple_flag_get_must_write(fi) && !predicated)
                inf.kills |= LV_FLAGS;
        }
    }

    xed_category_enum_t cat = (xed_category_enum_t)INS_Category(ins);
    inf.is_ret = INS_IsRet(ins);
    bool is_call = INS_IsCall(ins);
    inf.is_cond_br = (cat == XED_CATEGORY_COND_BR);
    inf.is_direct_jmp = (cat == XED_CATEGORY_UNCOND_BR && INS_IsDirectControlFlow(ins));
    inf.is_indirect_jmp = (INS_IsIndirectControlFlow(ins) && !is_call && !inf.is_ret);
    if (INS_IsDirectControlFlow(ins))
        inf.direct_targ = INS_DirectControlFlowTargetAddress(ins);

    if (is_call) {
        // SysV ABI across a call: RAX (varargs count in AL) and RCX (4th
        // argument) may be read by the callee; RAX/RCX/flags come back
        // clobbered; RBX is callee-saved and flows through transparently.
        inf.reads |= LV_RAX | LV_RCX;
        inf.kills |= LV_RAX | LV_RCX | LV_FLAGS;
    }
    if (INS_IsSyscall(ins) || INS_IsInterrupt(ins) ||
        cat == XED_CATEGORY_SYSCALL || cat == XED_CATEGORY_SYSRET) {
        // Kernel entry: be maximally conservative.
        inf.reads |= LV_ALL;
    }

    rtn_ins_index[inf.addr] = (unsigned)rtn_ins_info.size();
    rtn_ins_info.push_back(inf);
}

/**************************/
/* compute_rtn_liveness() */
/**************************/
static void compute_rtn_liveness()
{
    int n = (int)rtn_ins_info.size();
    rtn_live_in.assign(n, 0);

    // Backward fixpoint; live sets only grow, so this converges.
    bool changed = true;
    while (changed) {
        changed = false;
        for (int i = n - 1; i >= 0; i--) {
            live_ins_info_t &inf = rtn_ins_info[i];
            unsigned fallthru = (i + 1 < n) ? rtn_live_in[i + 1] : LV_ALL;
            unsigned out;
            if (inf.is_ret) {
                // RAX carries the return value; RBX is callee-saved and the
                // caller relies on it. RCX (caller-saved) and the status
                // flags are dead across a return per the ABI.
                out = LV_RAX | LV_RBX;
            } else if (inf.is_indirect_jmp) {
                // Unknown target: registers conservatively live, and the
                // status flags too -- -O3 code (e.g. sgcc_peak) keeps a
                // comparison result in the flags across an indirect jump
                // and branches on it at the jump target, so the stub must
                // preserve them (its masking uses flag-free SHLX/SHRX).
                out = LV_RAX | LV_RBX | LV_RCX | LV_FLAGS;
            } else if (inf.is_direct_jmp) {
                std::map<ADDRINT, unsigned>::iterator it =
                    rtn_ins_index.find(inf.direct_targ);
                out = (it != rtn_ins_index.end()) ? rtn_live_in[it->second] : LV_ALL;
            } else if (inf.is_cond_br) {
                std::map<ADDRINT, unsigned>::iterator it =
                    rtn_ins_index.find(inf.direct_targ);
                out = fallthru |
                      ((it != rtn_ins_index.end()) ? rtn_live_in[it->second] : LV_ALL);
            } else {
                // Regular instructions and calls fall through (a call's
                // callee effect is modeled in its reads/kills).
                out = fallthru;
            }
            unsigned in = inf.reads | (out & ~inf.kills);
            if (in != rtn_live_in[i]) {
                rtn_live_in[i] = in;
                changed = true;
            }
        }
    }

    live_in_at_addr.clear();
    for (int i = 0; i < n; i++)
        live_in_at_addr[rtn_ins_info[i].addr] = rtn_live_in[i];
}

/*****************************/
/* dead_regs_at_orig_addr()  */
/*****************************/
// Registers that are provably dead right before the original instruction
// at addr executes. Unknown address => nothing is dead (conservative).
static unsigned dead_regs_at_orig_addr(ADDRINT addr)
{
    std::map<ADDRINT, unsigned>::iterator it = live_in_at_addr.find(addr);
    if (it == live_in_at_addr.end())
        return 0;
    return LV_ALL & ~it->second;
}

// Task 2 statistics, reported once at the end of translation.
static struct {
    unsigned stubs_total;
    unsigned stubs_mem_add;     // whole increment collapsed to ADD [counter],1
    unsigned stubs_rax_skipped; // RAX save/restore omitted
    unsigned stubs_rbx_skipped;
    unsigned stubs_rcx_skipped;
} opt_stats = {0, 0, 0, 0, 0};

/**************************/
/* add_profiling_instrs() */
/**************************/
int add_profiling_instrs(INS ins, ADDRINT ins_addr,
                         UINT64 *counter_addr, unsigned bbl_num,
                         unsigned dead_mask)
{
  xed_encoder_instruction_t enc_instr;

  static uint64_t rax_mem = 0;

  unsigned stub_seq = opt_stats.stubs_total; // ordinal of this stub

  // Debug discriminator: same size as the unoptimized stub, same register
  // effect as the optimized one (RAX ends up clobbered).
  bool dummy_clobber = KnobDummyClobber &&
                       (dead_mask & LV_RAX) &&
                       stub_seq >= KnobSkipLo.Value() &&
                       stub_seq < KnobSkipHi.Value();

  if (stub_seq == KnobReportStub.Value()) {
    cerr << "REPORT stub " << dec << stub_seq
         << " at orig 0x" << hex << ins_addr
         << " dead_mask=0x" << dead_mask
         << " counter=0x" << (ADDRINT)counter_addr
         << " bbl=" << dec << bbl_num
         << " ins: " << INS_Disassemble(ins) << "\n";
    RTN r = RTN_FindByAddress(ins_addr);
    if (RTN_Valid(r))
      cerr << "REPORT rtn: " << RTN_Name(r)
           << " [0x" << hex << RTN_Address(r)
           << " +0x" << RTN_Size(r) << "]\n";
  }

  if (KnobNoSkipDead || dummy_clobber ||
      stub_seq < KnobSkipLo.Value() || stub_seq >= KnobSkipHi.Value())
    dead_mask = 0;

  bool is_indirect = (INS_IsIndirectControlFlow(ins) &&
                      !INS_IsRet(ins) && !INS_IsCall(ins));

  bool rax_dead   = (dead_mask & LV_RAX)   != 0;
  bool rbx_dead   = (dead_mask & LV_RBX)   != 0;
  bool rcx_dead   = (dead_mask & LV_RCX)   != 0;
  bool flags_dead = (dead_mask & LV_FLAGS) != 0;

  // For indirect-jump stubs, retrieve the details of the jump operand up
  // front (also needed to decide whether RAX must be preserved).
  xed_reg_enum_t base_reg = XED_REG_INVALID;
  xed_reg_enum_t index_reg = XED_REG_INVALID;
  xed_reg_enum_t targ_reg = XED_REG_INVALID;
  xed_int64_t disp = 0;
  xed_uint_t scale = 0;
  xed_uint_t width = 0;
  unsigned mem_addr_width = 0;
  if (is_indirect) {
    xed_decoded_inst_t *xedd = INS_XedDec(ins);
    base_reg = xed_decoded_inst_get_base_reg(xedd, 0);
    index_reg = xed_decoded_inst_get_index_reg(xedd, 0);
    disp = xed_decoded_inst_get_memory_displacement(xedd, 0);
    scale = xed_decoded_inst_get_scale(xedd, 0);
    width = xed_decoded_inst_get_memory_displacement_width_bits(xedd, 0);
    mem_addr_width = xed_decoded_inst_get_memop_address_width(xedd, 0);
    unsigned memops = xed_decoded_inst_number_of_memory_operands(xedd);
    if (!memops)
      targ_reg = xed_decoded_inst_get_reg(xedd, XED_OPERAND_REG0);
  }
  bool targ_uses_rax = (targ_reg == XED_REG_RAX || base_reg == XED_REG_RAX ||
                        index_reg == XED_REG_RAX);

  // When the status flags are dead, the whole RAX-based counter increment
  // collapses into a single RIP-relative  ADD qword ptr [counter], 1.
  // It encodes as REX.W 83 /0 disp32 imm8 = 8 bytes and is usable only when
  // the counter lies within a 32-bit displacement of the code (bbl_map is
  // allocated inside the TC mmap region to make that hold). Indirect-jump
  // stubs get no special treatment: the flags can be LIVE across an
  // indirect jump (sgcc_peak keeps a comparison result in them), so the
  // ADD, which writes the flags, is only safe when they are provably dead.
  const unsigned MEM_ADD_LEN = 8;
  xed_int64_t add_disp =
      (xed_int64_t)((ADDRINT)counter_addr - ins_addr - MEM_ADD_LEN);
  bool use_mem_add = flags_dead && !KnobNoMemAdd &&
                     add_disp <= 0x7FFFFFFFLL && add_disp >= -0x7FFFFFFFLL;

  // RAX is needed by the LEA-based increment and by the indirect-target
  // bucketing; preserve it only if its current value is still live.
  bool need_rax = is_indirect || !use_mem_add;
  bool save_rax = (need_rax && !rax_dead) || (is_indirect && targ_uses_rax);

  opt_stats.stubs_total++;
  if (use_mem_add) opt_stats.stubs_mem_add++;
  if (need_rax && !save_rax) opt_stats.stubs_rax_skipped++;

  // Add NOP instr (to be overwritten later on by a jmp that skips
  // the profiling, once profiling is done).
  xed_inst0(&enc_instr, dstate, XED_ICLASS_NOP4, 64);
  if (add_new_encoded_instr(ins_addr, &enc_instr, ProfilingIns) < 0)
    return -1;

  // Save RAX (only when its current value is still needed):
  // MOV RAX into rax_mem
  if (save_rax) {
    xed_inst2(&enc_instr, dstate, XED_ICLASS_MOV, 64,
              xed_mem_bd(XED_REG_INVALID, xed_disp((ADDRINT)&rax_mem, 64), 64), // Destination op.
              xed_reg(XED_REG_RAX));
    if (add_new_encoded_instr(ins_addr, &enc_instr, ProfilingIns) < 0)
      return -1;
  }

  // Create profiling for indirect jump targets.
  if (is_indirect) {
    // Debug print.
    //cerr << " BBL terminates with indirect jump: "
    //     << " 0x" << hex << ins_addr << ": "
    //     << INS_Disassemble(ins) << "\n";

    static uint64_t rbx_mem = 0;
    static uint64_t rcx_mem = 0;

    // Debug print.
    //dump_instr_from_xedd(xedd, ins_addr);
    //cerr << " base reg: " << xed_reg_enum_t2str(base_reg)
    //     << " index reg " << xed_reg_enum_t2str(index_reg)
    //     << " scale: " << dec << scale
    //     << " disp: 0x" << hex << disp
    //     << " width: " << dec << width
    //     << " mem addr width: " << dec << mem_addr_width
    //     << " targ reg: " << targ_reg << xed_reg_enum_t2str(targ_reg)
    //     << "\n";
    
    // save RBX into rbx_mem in 2 steps via RAX
    // save RCX into rcx_mem in 2 steps via RAX
    // Convert jmp [base_reg + index_reg*scale] to: MOV RAX, [base_reg + index_reg*scale]
    //         Or convert jmp targ_reg to: MOV RAX, targ_reg ==> RAX holds jump targ addr
    // MOV RBX, RAX ==> Now RBX also holds targ addr
    // AND RAX, MAX_TARG_ADDR ==> RAX holds index i = 0..MAX_TARG_ADDRS
    // MOV RCX, xed_imm0((ADDRINT)&bbl_map_targ_addr[bbl_num][0])
    // MOV [RCX + 8*RAX], RBX
    // MOV RBX, xed_imm0((ADDRINT)&bbl_map_targ_count[bbl_num][0])
    // MOV RCX, [RBX + 8*RAX]
    // LEA RCX, [RCX + 1]
    // MOV [RBX + 8*RAX], RCX
    // restore RCX from rcx_mem in 2 steps via RAX
    // restore RBX from rbx_mem in 2 steps via RAX
    
    // Save RBX in 2 steps via RAX (skipped when RBX is dead here):
    if (!rbx_dead) {
      // Save RBX step 1 - MOV RBX into RAX
      xed_inst2(&enc_instr, dstate, XED_ICLASS_MOV, 64,
                xed_reg(XED_REG_RAX),  // Destination op.
                xed_reg(XED_REG_RBX));
      if (add_new_encoded_instr(ins_addr, &enc_instr, ProfilingIns) < 0)
        return -1;

      // Save RBX step 2 - MOV RAX into rbx_mem
      xed_inst2(&enc_instr, dstate, XED_ICLASS_MOV, 64,
                xed_mem_bd(XED_REG_INVALID, xed_disp((ADDRINT)&rbx_mem, 64), 64), // Destination op.
                xed_reg(XED_REG_RAX));
      if (add_new_encoded_instr(ins_addr, &enc_instr, ProfilingIns) < 0)
        return -1;
    } else {
      opt_stats.stubs_rbx_skipped++;
    }

    // Save RCX in 2 steps via RAX (skipped when RCX is dead here):
    if (!rcx_dead) {
      // Save RCX step 1 - MOV RCX into RAX
      xed_inst2(&enc_instr, dstate, XED_ICLASS_MOV, 64,
                xed_reg(XED_REG_RAX),   // Destination op.
                xed_reg(XED_REG_RCX));
      if (add_new_encoded_instr(ins_addr, &enc_instr, ProfilingIns) < 0)
        return -1;

      // Save RCX step 2 - MOV RAX into rcx_mem
      xed_inst2(&enc_instr, dstate, XED_ICLASS_MOV, 64,
                xed_mem_bd(XED_REG_INVALID, xed_disp((ADDRINT)&rcx_mem, 64), 64), // Destination op.
                xed_reg(XED_REG_RAX));
      if (add_new_encoded_instr(ins_addr, &enc_instr, ProfilingIns) < 0)
        return -1;
    } else {
      opt_stats.stubs_rcx_skipped++;
    }

    // Replace RIP reg by an absolute displacement.
    // Convert 'jmp [rax*8+0x657118]' or: 'jmp [rip+0x42513c]'
    // to: mov rax, [rax*8+0x657118] or: mov rax, [<absolute addr>]
    //
    // Check if we need to restore RAX in case  it is used as base reg or index reg,
    // e.g., jmp [RIP+8*RAX] or: jmp [RAX+8*RBX]
    
    // Check if we need to restore RAX from rax_mem.
    if (targ_uses_rax) {
      xed_inst2(&enc_instr, dstate, XED_ICLASS_MOV, 64,
                xed_reg(XED_REG_RAX), // Destination reg op.
                xed_mem_bd(XED_REG_INVALID, xed_disp((ADDRINT)&rax_mem, 64), 64));
      if (add_new_encoded_instr(ins_addr, &enc_instr, ProfilingIns) < 0)
       return -1;
    }
    // Check if we need to convert [RIP+disp+index*scale] to [absolute_disp + index*scale]
    if (base_reg == XED_REG_RIP) {
      unsigned int orig_size = INS_Size(ins);
      // Modify rip displacement by an absolute displacement val.
      xed_int64_t new_disp = ins_addr + disp + orig_size;
      if (new_disp > 0x7FFFFFFF || new_disp < -0x7FFFFFFF) {
        // PIE binaries (e.g. cpugcc_r_base.Oz-m64) are loaded at high
        // addresses, so the absolute address of a rip-based jump table
        // does not fit in a 32-bit displacement. Load it into a scratch
        // register with a 64-bit immediate MOV and use that register as
        // the base instead. RBX and RCX are both clobbered by the
        // indirect-target code below anyway (their original values were
        // saved above when live; clobbering a dead register is safe by
        // definition); pick the one that is not the jump's index register.
        xed_reg_enum_t scratch_reg =
            (index_reg == XED_REG_RBX ? XED_REG_RCX : XED_REG_RBX);
        xed_inst2(&enc_instr, dstate, XED_ICLASS_MOV, 64,
                  xed_reg(scratch_reg),    // Destination reg op.
                  xed_imm0((xed_uint64_t)new_disp, 64));
        if (add_new_encoded_instr(ins_addr, &enc_instr, ProfilingIns) < 0)
          return -1;
        xed_inst2(&enc_instr, dstate, XED_ICLASS_MOV, 64,
                  xed_reg(XED_REG_RAX),    // Destination reg op.
                  xed_mem_bisd(scratch_reg, index_reg, scale,
                               xed_disp(0, 32),
                               mem_addr_width));
      } else {
        xed_int64_t new_disp_width = 32; // set maximal disp width for now.
        xed_inst2(&enc_instr, dstate, XED_ICLASS_MOV, 64,
                  xed_reg(XED_REG_RAX),    // Destination reg op.
                  xed_mem_bisd(XED_REG_INVALID, index_reg, scale,
                               xed_disp(new_disp, new_disp_width),
                               mem_addr_width));
      }
    } else if (targ_reg != XED_REG_RAX) { // avoid ceating the MOV RAX, RAX Nop.
        xed_inst2(&enc_instr, dstate, XED_ICLASS_MOV, 64,
                 xed_reg(XED_REG_RAX),    // Destination reg op.
                 (targ_reg != XED_REG_INVALID ? xed_reg(targ_reg) :
                  xed_mem_bisd(base_reg, index_reg, scale, xed_disp(disp, width), mem_addr_width)));
    }
    if (add_new_encoded_instr(ins_addr, &enc_instr, ProfilingIns) < 0)
      return -1;
    
    // MOV RBX, RAX
    xed_inst2(&enc_instr, dstate, XED_ICLASS_MOV, 64,
              xed_reg(XED_REG_RBX),    // Destination reg op.
              xed_reg(XED_REG_RAX));
    if (add_new_encoded_instr(ins_addr, &enc_instr, ProfilingIns) < 0)
      return -1;
    
    // RAX = RAX & MAX_TARG_ADDRS (keep only MAX_TARG_ADDRS+1 targets for
    // profiling), computed WITHOUT touching RFLAGS. The original AND
    // writes the flags, and the flags can be LIVE across the indirect
    // jump that terminates this BBL (sgcc_peak's -O3 code keeps a
    // comparison result in them and branches on it at the jump target,
    // which silently corrupted its output). SHLX/SHRX (BMI2) shift
    // without writing flags; RCX is a free shift-count register here --
    // its original value was already saved above (or is dead), and the
    // target-table code below overwrites it anyway.
    //
    // MOV RCX, 62   (62 = 64 minus the 2 mask bits of MAX_TARG_ADDRS=0x3)
    xed_inst2(&enc_instr, dstate, XED_ICLASS_MOV, 64,
              xed_reg(XED_REG_RCX),    // Destination reg op.
              xed_imm0(62, 32));
    if (add_new_encoded_instr(ins_addr, &enc_instr, ProfilingIns) < 0)
      return -1;

    // SHLX RAX, RAX, RCX
    xed_inst3(&enc_instr, dstate, XED_ICLASS_SHLX, 64,
              xed_reg(XED_REG_RAX),    // Destination reg op.
              xed_reg(XED_REG_RAX),
              xed_reg(XED_REG_RCX));
    if (add_new_encoded_instr(ins_addr, &enc_instr, ProfilingIns) < 0)
      return -1;

    // SHRX RAX, RAX, RCX
    xed_inst3(&enc_instr, dstate, XED_ICLASS_SHRX, 64,
              xed_reg(XED_REG_RAX),    // Destination reg op.
              xed_reg(XED_REG_RAX),
              xed_reg(XED_REG_RCX));
    if (add_new_encoded_instr(ins_addr, &enc_instr, ProfilingIns) < 0)
      return -1;
    
    // MOV RCX, xed_imm0((ADDRINT)&bbl_map[bbl_num].targ_addr[0])
    xed_inst2(&enc_instr, dstate, XED_ICLASS_MOV, 64,
              xed_reg(XED_REG_RCX), // Destination reg op.
              xed_imm0((ADDRINT)&(bbl_map[bbl_num].targ_addr[0]), 64));
    if (add_new_encoded_instr(ins_addr, &enc_instr, ProfilingIns) < 0)
      return -1;
    
    // MOV [RCX + 8*RAX], RBX
    xed_inst2(&enc_instr, dstate, XED_ICLASS_MOV, 64,
              xed_mem_bisd(XED_REG_RCX, // base reg
                           XED_REG_RAX, //index reg
                           8, // scale
                           xed_disp(0, 32), // disp
                           64),  // Destination reg op.
              xed_reg(XED_REG_RBX));
    if (add_new_encoded_instr(ins_addr, &enc_instr, ProfilingIns) < 0)
      return -1;
    
    // MOV RBX, xed_imm0((ADDRINT)&bbl_map[bbl_num].targ_count[0])
    xed_inst2(&enc_instr, dstate, XED_ICLASS_MOV, 64,
              xed_reg(XED_REG_RBX), // Destination reg op.
              xed_imm0((ADDRINT)&(bbl_map[bbl_num].targ_count[0]), 64));
    if (add_new_encoded_instr(ins_addr, &enc_instr, ProfilingIns) < 0)
      return -1;
    
    // MOV RCX, [RBX + 8*RAX]
    xed_inst2(&enc_instr, dstate, XED_ICLASS_MOV, 64,
              xed_reg(XED_REG_RCX),   // Destination reg op.
              xed_mem_bisd(XED_REG_RBX, // base reg
                           XED_REG_RAX, //index reg
                           8, // scale
                           xed_disp(0, 32), // disp
                           64));
    if (add_new_encoded_instr(ins_addr, &enc_instr, ProfilingIns) < 0)
      return -1;
    
    // LEA RCX, [RCX + 1]
    xed_inst2(&enc_instr, dstate, XED_ICLASS_LEA, 64,
              xed_reg(XED_REG_RCX), // Destination reg op.
              xed_mem_bd(XED_REG_RCX, // base reg
                         xed_disp(1, 8), // disp
                         64));
    if (add_new_encoded_instr(ins_addr, &enc_instr, ProfilingIns) < 0)
      return -1;
    
    // MOV [RBX + 8*RAX], RCX
    xed_inst2(&enc_instr, dstate, XED_ICLASS_MOV, 64,
              xed_mem_bisd(XED_REG_RBX, // base reg
                           XED_REG_RAX, //index reg
                           8, // scale
                           xed_disp(0, 32), // disp
                           64),     // Destination op.
              xed_reg(XED_REG_RCX));
    if (add_new_encoded_instr(ins_addr, &enc_instr, ProfilingIns) < 0)
      return -1;
    
    // Restore RCX (only if it was saved):
    if (!rcx_dead) {
      // Restore RCX step 1- MOV from rcx_mem into RAX
      xed_inst2(&enc_instr, dstate, XED_ICLASS_MOV, 64,
                xed_reg(XED_REG_RAX), // Destination op.
                xed_mem_bd(XED_REG_INVALID, xed_disp((ADDRINT)&rcx_mem, 64), 64));
      if (add_new_encoded_instr(ins_addr, &enc_instr, ProfilingIns) < 0)
        return -1;

      // Restore RCX step 2 - MOV RAX into RCX
      xed_inst2(&enc_instr, dstate, XED_ICLASS_MOV, 64,
                xed_reg(XED_REG_RCX),  // Destination op.
                xed_reg(XED_REG_RAX));
      if (add_new_encoded_instr(ins_addr, &enc_instr, ProfilingIns) < 0)
        return -1;
    }

    // Restore RBX (only if it was saved):
    if (!rbx_dead) {
      // Restore RBX step 1 - MOV from rbx_mem into RAX
      xed_inst2(&enc_instr, dstate, XED_ICLASS_MOV, 64,
                xed_reg(XED_REG_RAX), // Destination op.
                xed_mem_bd(XED_REG_INVALID, xed_disp((ADDRINT)&rbx_mem, 64), 64));
      if (add_new_encoded_instr(ins_addr, &enc_instr, ProfilingIns) < 0)
        return -1;

      // Restore RBX step 2 - MOV RAX into RBX
      xed_inst2(&enc_instr, dstate, XED_ICLASS_MOV, 64,
                xed_reg(XED_REG_RBX),  // Destination op.
                xed_reg(XED_REG_RAX));
      if (add_new_encoded_instr(ins_addr, &enc_instr, ProfilingIns) < 0)
        return -1;
    }

  } // end of: 'if bbl terminates with indirect jump'.

  // Create the profiling instrs for counting the BBL frequency.
  //
  if (use_mem_add) {
    // Status flags are provably dead here, so the increment is a single
    // flag-modifying memory ADD that needs no register at all:
    //   ADD qword ptr [counter], 1  (RIP-relative).
    xed_inst2(&enc_instr, dstate, XED_ICLASS_ADD, 64,
              xed_mem_bd(XED_REG_RIP, xed_disp(add_disp, 32), 64), // Destination op.
              xed_imm0(1, 8));
    if (add_new_encoded_instr(ins_addr, &enc_instr, ProfilingIns) < 0)
      return -1;
    // The RIP fixup machinery later recomputes the displacement from
    // orig_rip_addr (= ins_addr + disp + size); verify the encoding landed
    // exactly on the counter so that fixup cannot go astray.
    if (instr_map[num_of_instr_map_entries - 1].size != MEM_ADD_LEN ||
        instr_map[num_of_instr_map_entries - 1].orig_rip_addr != (ADDRINT)counter_addr) {
      cerr << "ERROR: unexpected encoding of profiling ADD [counter],1 at: 0x"
           << hex << ins_addr << endl;
      return -1;
    }
  } else {
    // Flags are live: use the flag-transparent MOV/LEA/MOV sequence via RAX.

    // MOV from bbl_map into RAX
    xed_inst2(&enc_instr, dstate, XED_ICLASS_MOV, 64,
              xed_reg(XED_REG_RAX),  // Destination reg op.
              xed_mem_bd(XED_REG_INVALID, xed_disp((ADDRINT)counter_addr, 64), 64));
    if (add_new_encoded_instr(ins_addr, &enc_instr, ProfilingIns) < 0)
      return -1;

    // LEA RAX, [RAX+1]
    xed_inst2(&enc_instr, dstate, XED_ICLASS_LEA,  64,  // operand width
              xed_reg(XED_REG_RAX), // Destination reg op.
              xed_mem_bd(XED_REG_RAX, xed_disp(1, 8), 64));
    if (add_new_encoded_instr(ins_addr, &enc_instr, ProfilingIns) < 0)
      return -1;

    // MOV from RAX into bbl_map
    xed_inst2(&enc_instr, dstate, XED_ICLASS_MOV, 64,
              xed_mem_bd(XED_REG_INVALID, xed_disp((ADDRINT)counter_addr, 64), 64), // Destination op.
              xed_reg(XED_REG_RAX));
    if (add_new_encoded_instr(ins_addr, &enc_instr, ProfilingIns) < 0)
      return -1;
  }

  // Restore RAX (only if it was saved):
  // MOV from rax_mem into RAX
  if (save_rax) {
    static uint64_t dummy_mem = 0x4242424242424242ULL;
    xed_inst2(&enc_instr, dstate, XED_ICLASS_MOV, 64,
              xed_reg(XED_REG_RAX), // Destination reg op.
              xed_mem_bd(XED_REG_INVALID,
                         xed_disp((ADDRINT)(dummy_clobber ? &dummy_mem : &rax_mem), 64),
                         64));
    if (add_new_encoded_instr(ins_addr, &enc_instr, ProfilingIns) < 0)
      return -1;
  } else if (need_rax && KnobPoisonRax) {
    // Debug aid: make an unsound "RAX is dead" decision immediately
    // recognizable in a crash by loading a marker value.
    xed_inst2(&enc_instr, dstate, XED_ICLASS_MOV, 64,
              xed_reg(XED_REG_RAX),
              xed_imm0(0x4242424242424242ULL, 64));
    if (add_new_encoded_instr(ins_addr, &enc_instr, ProfilingIns) < 0)
      return -1;
  }

  return 0;
}

/**************************************************/
/* chain_all_direct_jmp_and_call_target_entries() */
/**************************************************/
void chain_all_direct_jmp_and_call_target_entries(unsigned from_entry,
                                                 unsigned until_entry)
{
    entry_map.clear();

    for (unsigned i = from_entry; i < until_entry; i++) {
        instr_map[i].targ_map_entry = -1;
        ADDRINT orig_ins_addr = instr_map[i].orig_ins_addr;
        if (!orig_ins_addr)
          continue;
        // For instrs with same orig_addr, give precedence to the first one.
        entry_map.emplace(orig_ins_addr, i);
    }

    for (unsigned i = from_entry; i < until_entry; i++) {
        ADDRINT orig_targ_addr = instr_map[i].orig_targ_addr;
        if (orig_targ_addr == 0)
            continue;
        if (instr_map[i].targ_map_entry > 0)
            continue;
        if (!entry_map.count(orig_targ_addr))
            continue;
        if (!instr_map[i].size)
            continue;
        instr_map[i].targ_map_entry = entry_map[orig_targ_addr];
    }
}


/***********************************************/
/* set_initial_estimated_new_ins_addrs_in_tc() */
/***********************************************/
int set_initial_estimated_new_ins_addrs_in_tc(char *tc) {
  unsigned tc_cursor = 0;
  // Set initial estimated new addrs for each instruction in the tc.
  for (unsigned i=0; i < num_of_instr_map_entries; i++) {
    instr_map[i].new_ins_addr = (ADDRINT)&tc[tc_cursor];
    // update expected size of tc.
    tc_cursor += instr_map[i].size;
    // Check if we exceeded the TC size.
    if (tc_cursor >= max_tc_size)
      return -1;
  }
  return 0;
}


/**************************/
/* fix_rip_displacement() */
/**************************/
int fix_rip_displacement(unsigned instr_map_entry)
{
    // uncond jumps instructions with size=0
    // should remain with size=0 for beeing removed from tc
    if (!instr_map[instr_map_entry].size)
        return 0;

    // Check if it is a RIP-relative instr.
    if (!instr_map[instr_map_entry].orig_rip_addr)
      return 0;

    // Check if it is a direct jmp or call instruction.
    if (instr_map[instr_map_entry].orig_targ_addr != 0)
      return 0;

    xed_decoded_inst_t xedd;
    xed_decoded_inst_zero_set_mode(&xedd, &dstate);

    xed_error_enum_t xed_code =
       xed_decode(&xedd, reinterpret_cast<UINT8*>(instr_map[instr_map_entry].encoded_ins), max_inst_len);
    if (xed_code != XED_ERROR_NONE) {
        cerr << "ERROR: xed decode failed for instr at: " << "0x"
             << hex << instr_map[instr_map_entry].new_ins_addr << endl;
        return -1;
    }

    //debug print:
    if (KnobVerbose) {
      cerr << " Before fixing rip offset\n";
      dump_instr_map_entry(instr_map_entry);
    }

    //xed_uint_t disp_byts = xed_decoded_inst_get_memory_displacement_width(xedd,i); // how many byts in disp ( disp length in byts - for example FFFFFFFF = 4
    xed_int64_t new_disp = 0;
    xed_uint_t new_disp_byts = 4;   // set maximal num of byts for now.

    // Modify rip displacement. use rip-relative direct addressing mode.
    new_disp = (xed_int64_t)(instr_map[instr_map_entry].orig_rip_addr - instr_map[instr_map_entry].new_ins_addr -
                               instr_map[instr_map_entry].size);
    // Code when using direct addressing mode.
    //xed_encoder_request_set_base0 (&xedd, XED_REG_INVALID);
    //new_disp = instr_map[instr_map_entry].orig_rip_addr;
    if (new_disp > 0x7FFFFFFF || new_disp < -0x7FFFFFFF) {
        cerr << "Invalid rip displacement larger than 32 bits in fix_rip_displacement\n";
        dump_instr_map_entry(instr_map_entry);
        return -1;
    }

    // Set the memory displacement using a bit length.
    xed_encoder_request_set_memory_displacement (&xedd, new_disp, new_disp_byts);

    unsigned max_size = XED_MAX_INSTRUCTION_BYTES;
    unsigned new_size = 0;

    // Converts the decoder request to a valid encoder request:
    xed_encoder_request_init_from_decode (&xedd);

    xed_error_enum_t xed_error =
       xed_encode (&xedd, reinterpret_cast<UINT8*>(instr_map[instr_map_entry].encoded_ins),
                   max_size , &new_size);
    if (xed_error != XED_ERROR_NONE) {
        cerr << "ENCODE ERROR: " << xed_error_enum_t2str(xed_error) << endl;
        dump_instr_map_entry(instr_map_entry);
        return -1;
    }

    //debug print:
    if (KnobVerbose) {
      cerr << " After fixing rip offset\n";
      dump_instr_map_entry(instr_map_entry);
    }

    return new_size;
}


/**************************************/
/* fix_direct_jmp_or_call_to_orig_addr */
/**************************************/
int fix_direct_jmp_or_call_to_orig_addr(unsigned instr_map_entry)
{
    // Ignore instructions of zero size.
    if (!instr_map[instr_map_entry].size)
      return 0;

    // Debug print.
    cerr << "jump to orig addr: 0x" << hex << instr_map[instr_map_entry].orig_targ_addr << " : ";
    dump_instr_from_mem ((ADDRINT *)instr_map[instr_map_entry].encoded_ins,
                         instr_map[instr_map_entry].orig_ins_addr);

    // check for cases of direct jumps/calls back to the orginal target address:
    if (instr_map[instr_map_entry].targ_map_entry >= 0) {
        cerr << "ERROR: Invalid jump or call instruction" << endl;
        return -1;
    }

    xed_decoded_inst_t xedd;
    xed_decoded_inst_zero_set_mode(&xedd,&dstate);

    xed_error_enum_t xed_code =
        xed_decode(&xedd, reinterpret_cast<UINT8*>(instr_map[instr_map_entry].encoded_ins), max_inst_len);
    if (xed_code != XED_ERROR_NONE) {
        cerr << "ERROR: xed decode failed for instr at: " << "0x"
             << hex << instr_map[instr_map_entry].new_ins_addr << endl;
        return -1;
    }

    xed_category_enum_t category_enum = xed_decoded_inst_get_category(&xedd);

    if (category_enum != XED_CATEGORY_CALL && category_enum != XED_CATEGORY_UNCOND_BR) {
        cerr << "ERROR: Invalid direct jump from translated code to original code for:\n";
        dump_instr_map_entry(instr_map_entry);
        return -1;
    }

    unsigned ilen = XED_MAX_INSTRUCTION_BYTES;
    unsigned olen = 0;

    xed_encoder_instruction_t  enc_instr;

    // Use the heap variable instr_map[instr_map_entry].orig_targ_addr as the
    // memory container that holds the target address for the jmp/call
    // and indirectly jmp/call via that memory location.

    // search for orig_targ_addr in jump_to_orig_addr_map.
    int jump_to_orig_addr_map_entry = -1;
    for (unsigned i = 0; i < jump_to_orig_addr_num; i++) {
      if (instr_map[instr_map_entry].orig_targ_addr == jump_to_orig_addr_map[i]) {
        jump_to_orig_addr_map_entry = i;
        break;
      }
    }
    if (jump_to_orig_addr_map_entry < 0) {
      jump_to_orig_addr_num++;
      jump_to_orig_addr_map_entry = jump_to_orig_addr_num;
      if ((unsigned)jump_to_orig_addr_map_entry >= max_rtn_count) {
         cerr << "exceeded size of jump_to_orig_addr_map at fix_direct_jmp_or_call_to_orig_addr\n";
         return -1;
      }
      jump_to_orig_addr_map[jump_to_orig_addr_map_entry] = instr_map[instr_map_entry].orig_targ_addr;
    }

    xed_int64_t new_disp = (ADDRINT)&jump_to_orig_addr_map[jump_to_orig_addr_map_entry] -
                       instr_map[instr_map_entry].new_ins_addr -
                       xed_decoded_inst_get_length (&xedd);
    if (new_disp > 0x7FFFFFFF || new_disp < -0x7FFFFFFF) {
        cerr << "Invalid rip displacement larger than 32 bits in fix_direct_jmp_or_call_to_orig_addr\n";
        cerr << "new displacement: " << dec << new_disp << "\n";
        return -1;
    }

    if (category_enum == XED_CATEGORY_CALL)
            xed_inst1(&enc_instr, dstate,
            XED_ICLASS_CALL_NEAR, 64,
            xed_mem_bd (XED_REG_RIP, xed_disp(new_disp, 32), 64));

    if (category_enum == XED_CATEGORY_UNCOND_BR)
            xed_inst1(&enc_instr, dstate,
            XED_ICLASS_JMP, 64,
            xed_mem_bd (XED_REG_RIP, xed_disp(new_disp, 32), 64));

    xed_encoder_request_t enc_req;

    xed_encoder_request_zero_set_mode(&enc_req, &dstate);
    xed_bool_t convert_ok = xed_convert_to_encoder_request(&enc_req, &enc_instr);
    if (!convert_ok) {
        cerr << "conversion to encode request failed" << endl;
        return -1;
    }

    xed_error_enum_t xed_error =
       xed_encode(&enc_req, reinterpret_cast<UINT8*>(instr_map[instr_map_entry].encoded_ins), ilen, &olen);
    if (xed_error != XED_ERROR_NONE) {
        cerr << "ENCODE ERROR: " << xed_error_enum_t2str(xed_error) << endl;
        dump_instr_map_entry(instr_map_entry);
        return -1;
    }

    // NOTE: We cannot zero the orig_targ_addr field in instr_map as follows:
    //  instr_map[instr_map_entry].orig_targ_addr = 0x0;
    // This is because the RIP displacement may become too large to fit into 4 bytes long.

    // debug prints:
    if (KnobVerbose) {
        dump_instr_map_entry(instr_map_entry);
    }

    return olen;
}


/**************************************/
/* fix_direct_jmp_or_call_displacement */
/**************************************/
int fix_direct_jmp_or_call_displacement(unsigned instr_map_entry)
{
    //uncond jumps instructions with size=0 should remain with size=0
    // for beeing removed from tc
    if (!instr_map[instr_map_entry].size)
        return 0;

    // Check if it is indeed a direct branch or a direct call instr:
    if (instr_map[instr_map_entry].orig_targ_addr == 0)
      return 0;

    xed_decoded_inst_t xedd;
    xed_decoded_inst_zero_set_mode(&xedd,&dstate);

    xed_error_enum_t xed_code =
        xed_decode(&xedd, reinterpret_cast<UINT8*>(instr_map[instr_map_entry].encoded_ins), max_inst_len);
    if (xed_code != XED_ERROR_NONE) {
        cerr << "ERROR: xed decode failed for instr at: "
             << "0x" << hex << instr_map[instr_map_entry].new_ins_addr << endl;
        return -1;
    }

    xed_int64_t  new_disp = 0;
    unsigned max_size = XED_MAX_INSTRUCTION_BYTES;
    unsigned new_size = 0;


    xed_category_enum_t category_enum = xed_decoded_inst_get_category(&xedd);

    if (category_enum != XED_CATEGORY_CALL &&
        category_enum != XED_CATEGORY_COND_BR &&
        category_enum != XED_CATEGORY_UNCOND_BR) {
        cerr << "ERROR: unrecognized branch displacement" << endl;
        return -1;
    }

    // fix direct branches/calls to original targ addresses or
    // indirect branches via a rip offset which had previously been
    // formed by previouis calls to fix_direct_jmp_or_call_to_orig_addr()
    // in order to relpace direct jumps to orig targ addrs.
    ADDRINT new_targ_addr;
    if (instr_map[instr_map_entry].targ_map_entry >= 0) {
       new_targ_addr = instr_map[instr_map[instr_map_entry].targ_map_entry].new_ins_addr;
    } else if (category_enum == XED_CATEGORY_COND_BR) {
       // Jcc whose target was not translated (it lies in a routine we skipped).
       // Jcc has no memory-indirect form, so it cannot be routed through
       // jump_to_orig_addr_map like CALL/JMP. Point the rel32 back at the
       // original target instead; the displacement check below verifies it fits.
       new_targ_addr = instr_map[instr_map_entry].orig_targ_addr;
    } else {
       int rc = fix_direct_jmp_or_call_to_orig_addr(instr_map_entry);
       return rc;
    }

    new_disp =
      (new_targ_addr - instr_map[instr_map_entry].new_ins_addr) - instr_map[instr_map_entry].size; // orig_size;
     if (new_disp > 0x7FFFFFFF || new_disp < -0x7FFFFFFF) {
        cerr << "Invalid rip displacement larger than 32 bits in fix_direct_jmp_or_call_displacement\n";
        return -1;
    }

    xed_uint_t   new_disp_byts = 4; // num_of_bytes(new_disp);  ???

    // the max displacement size of loop instructions is 1 byte:
    xed_iclass_enum_t iclass_enum = xed_decoded_inst_get_iclass(&xedd);
    if (iclass_enum == XED_ICLASS_LOOP ||
        iclass_enum == XED_ICLASS_LOOPE ||
        iclass_enum == XED_ICLASS_LOOPNE) {
      new_disp_byts = 1;
    }

    // the max displacement size of jecxz instructions is ???:
    xed_iform_enum_t iform_enum = xed_decoded_inst_get_iform_enum (&xedd);
    if (iform_enum == XED_IFORM_JRCXZ_RELBRb){
      new_disp_byts = 1;
    }

    // Converts the decoder request to a valid encoder request:
    xed_encoder_request_init_from_decode (&xedd);

    //Set the branch displacement:
    xed_encoder_request_set_branch_displacement (&xedd, new_disp, new_disp_byts);

    //xed_uint8_t enc_buf[XED_MAX_INSTRUCTION_BYTES];
    //xed_error_enum_t xed_error = xed_encode (&xedd, enc_buf, max_size , &new_size);
    xed_error_enum_t xed_error =
        xed_encode (&xedd, reinterpret_cast<UINT8*>(instr_map[instr_map_entry].encoded_ins), max_size, &new_size);
    if (xed_error != XED_ERROR_NONE) {
        cerr << "ENCODE ERROR: " << xed_error_enum_t2str(xed_error) <<  endl;
        char buf[2048];
        xed_format_context(XED_SYNTAX_INTEL, &xedd, buf, 2048,
                           static_cast<UINT64>(instr_map[instr_map_entry].orig_ins_addr), 0, 0);
        cerr << " instr: " << "0x" << hex << instr_map[instr_map_entry].orig_ins_addr << " : " << buf <<  endl;
          return -1;
    }

    //debug print of new instruction in tc:
    if (KnobVerbose) {
        dump_instr_map_entry(instr_map_entry);
    }

    return new_size;
}

/************************************/
/* fix_instructions_displacements() */
/************************************/
int fix_instructions_displacements()
{
   // fix displacemnets of direct branch or call instructions:

    int size_diff = 0;
    bool is_diff = false;

    do {

        size_diff = 0;
        is_diff = false;

        if (KnobVerbose) {
            cerr << "starting a pass of fixing instructions displacements: " << endl;
        }

        for (unsigned i=0; i < num_of_instr_map_entries; i++) {

            instr_map[i].new_ins_addr += size_diff;

            // fix rip displacement:
            int new_size = fix_rip_displacement(i);
            if (new_size) {
              if (new_size < 0)
                  return -1;
              if (instr_map[i].size != (unsigned)new_size) { // this was a rip-based instruction which was fixed.
                  if (instr_map[i].size < (unsigned)new_size)
                     size_diff += (new_size - instr_map[i].size);
                  else
                     size_diff -= (instr_map[i].size - new_size);
                  instr_map[i].size = (unsigned)new_size;
                  is_diff = true;
                  continue;
              }
            }

            // fix instr displacement for direct jump or call:
            new_size = fix_direct_jmp_or_call_displacement(i);
            if (new_size) {
              if (new_size < 0)
                  return -1;
              if (instr_map[i].size != (unsigned)new_size) {
                if (instr_map[i].size < (unsigned)new_size)
                   size_diff += (new_size - instr_map[i].size);
                else
                   size_diff -= (instr_map[i].size - new_size);
                instr_map[i].size = (unsigned)new_size;
                is_diff = true;
                continue;
              }
            }

        }  // end int i=0; i ..

    } while (is_diff);

   return 0;
 }


/********************************/
/* find_candidate_rtns_for_tc() */
/********************************/
int find_candidate_rtns_for_tc(IMG img)
{
    int rc = 0;
    // go over routines and check if they are candidates for translation and mark them for translation:

    for (SEC sec = IMG_SecHead(img); SEC_Valid(sec); sec = SEC_Next(sec))
    {
        if (!SEC_IsExecutable(sec) || SEC_IsWriteable(sec) || !SEC_Address(sec))
            continue;

        // .plt stubs and the ELF startup/teardown stubs in .init/.fini
        // (_init/_fini) are not normal functions; probe-replacing them
        // corrupts program startup, especially in statically-linked
        // binaries. Leave them running natively.
        if (SEC_Name(sec) == ".plt"     || SEC_Name(sec) == ".plt.got" ||
            SEC_Name(sec) == ".plt.sec" || SEC_Name(sec) == ".init"    ||
            SEC_Name(sec) == ".fini")
            continue;

        for (RTN rtn = SEC_RtnHead(sec); RTN_Valid(rtn); rtn = RTN_Next(rtn))
        {
            // Keep the entry num of the rtn head in case we need to
            // revert the insertin of the instruction in rtn into the instructions
            // map due to an invalid decoding.
            //unsigned rtn_entry = num_of_instr_map_entries;

            // Open the RTN.
            RTN_Open( rtn );

            // Map all instructions that are a target of some direct jump or call in the rtn.
            // In the same pass, collect the per-instruction register usage and
            // run the backward liveness analysis for this routine (Task 2).
            std::map<ADDRINT, bool>is_targ_map;
            is_targ_map.empty();
            rtn_ins_info.clear();
            rtn_ins_index.clear();
            bool rtn_has_short_branch = false;
            for (INS ins = RTN_InsHead(rtn); INS_Valid(ins); ins = INS_Next(ins)) {
               if (INS_IsDirectControlFlow(ins)) {
                 ADDRINT targ_addr = INS_DirectControlFlowTargetAddress(ins);
                 is_targ_map[targ_addr] = true;
               }
               // LOOP/LOOPcc/JRCXZ only have a 1-byte displacement. The
               // profiling stubs inflate branch distances, so their target
               // can move out of the +-127 range, and the re-encode then
               // silently wraps the displacement (jump lands 256/512 bytes
               // off). Leave routines containing them untranslated.
               xed_iclass_enum_t ic = (xed_iclass_enum_t)INS_Opcode(ins);
               if (ic == XED_ICLASS_LOOP || ic == XED_ICLASS_LOOPE ||
                   ic == XED_ICLASS_LOOPNE || ic == XED_ICLASS_JRCXZ ||
                   ic == XED_ICLASS_JECXZ)
                 rtn_has_short_branch = true;
               if (!KnobNoProfile)
                 collect_ins_liveness_info(ins);
            }
            if (rtn_has_short_branch) {
                cerr << "skipping rtn with 1-byte-displacement branch: "
                     << RTN_Name(rtn) << endl;
                RTN_Close(rtn);
                continue;
            }
            if (!KnobNoProfile)
              compute_rtn_liveness();

            for (INS ins = RTN_InsHead(rtn); INS_Valid(ins); ins = INS_Next(ins)) {

                //debug print of orig instruction:
                if (KnobVerbose) {
                    cerr << "old instr: ";
                    cerr << "0x" << hex << INS_Address(ins) << ": " << INS_Disassemble(ins) <<  endl;
                    //xed_print_hex_line(reinterpret_cast<UINT8*>(INS_Address (ins)), INS_Size(ins));
                }

                ADDRINT ins_addr = INS_Address(ins);

                xed_decoded_inst_t xedd;
                xed_error_enum_t xed_code;

                // Add instr into instr map:
                bool isRtnHeadIns = (RTN_Address(rtn) == ins_addr);
                ins_enum_t ins_type = (isRtnHeadIns ? RtnHeadIns : RegularIns);

                // Insert a NOP7 instr at Rtn Head to be used in order
                // to restore orig target of a cond jumps to a routine.
                //
                if (!KnobNoProfile && isRtnHeadIns) {
                  rc = create_nop7_xedd_instr(&xedd);
                  if (rc < 0) {
                    cerr << "ERROR: failed to create a NOP7 instr during translation of instr at: "
                         << "0x" << hex << ins_addr << endl;
                    RTN_Close(rtn);
                    return -1;
                  }
                  rc = add_new_instr_entry(&xedd, ins_addr, ins_type);
                  if (rc < 0) {
                    cerr << "ERROR: failed during instructon translation." << endl;
                    RTN_Close(rtn);
                    return -1;
                  }
                  ins_type = RegularIns;
                }

                // Check if ins is a control transfer instr that terminates a BBL
                // or the next instr is a target of a direct branch or call.
                INS next_ins = INS_Next(ins);
                bool isNextInsJumpTarget = 
                    (!INS_Valid(next_ins) ? false : is_targ_map[INS_Address(next_ins)]);
                bool isInsTerminatesBBL = (isJumpOrRet(ins) || isNextInsJumpTarget);

                // Add profiling instructions to count each BBL exec at runtime:
                //
                if (!KnobNoProfile) {
                  if (isInsTerminatesBBL) {
                    // The stub runs right before this instruction: registers
                    // dead at its live-in can be clobbered freely.
                    rc = add_profiling_instrs(ins, ins_addr, &bbl_map[bbl_num].counter, bbl_num,
                                              dead_regs_at_orig_addr(ins_addr));
                    if (rc < 0) {
                      RTN_Close(rtn);
                      return -1;
                    }
                  }
                }
          
                // Add ins to instr_map:
                //
                xed_decoded_inst_zero_set_mode(&xedd,&dstate);
                xed_code = xed_decode(&xedd, reinterpret_cast<UINT8*>(ins_addr), max_inst_len);
                if (xed_code != XED_ERROR_NONE) {
                    cerr << "ERROR: xed decode failed for instr at: " << "0x" << hex << ins_addr << endl;
                    RTN_Close(rtn);
                    return -1;
                }

                // Add the instr into the instr_map table.
                rc = add_new_instr_entry(&xedd, INS_Address(ins), ins_type);
                if (rc < 0) {
                    cerr << "ERROR: failed during instructon translation." << endl;
                    RTN_Close(rtn);
                    return -1;
                }

                if (isInsTerminatesBBL) {
                  bbl_map[bbl_num].terminating_ins_entry = num_of_instr_map_entries - 1;
                  bbl_num++;
                  if (bbl_num >= max_bbl_count) {
                    cerr << "out of memory for bbl_map" << endl;
                    RTN_Close(rtn);
                    return -1;
                  }
                  bbl_map[bbl_num].starting_ins_entry = num_of_instr_map_entries;
                }

                // Apply edge Profiling: For BBLs that end with a conditional branch,
                //     insert an increment of the fallthrough counter for this BBL,
                //     immediately after the cond branch which terminates the bbl.
                //     and before the next BBL.
                if (!KnobNoProfile && INS_Category(ins) == XED_CATEGORY_COND_BR) {
                  // The fall-through stub runs between the cond branch and the
                  // next instruction, so use the next instruction's live-in.
                  unsigned ft_dead = INS_Valid(next_ins) ?
                      dead_regs_at_orig_addr(INS_Address(next_ins)) : 0;
                  rc = add_profiling_instrs(ins, ins_addr,
                                            &bbl_map[bbl_num - 1].fallthru_counter, bbl_num-1,
                                            ft_dead);
                  if (rc < 0) {
                    RTN_Close(rtn);
                    return -1;
                  }
                }

            } // end for INS...

            // debug print of routine name:
            if (KnobVerbose) {
                cerr <<   "rtn name: " << RTN_Name(rtn) << endl;
            }

            // Handle routines whose last instruction can fall through past
            // the routine end (a non-branch instruction, a call, or the
            // fall-through side of a conditional branch). In the original
            // code execution simply continues at the next address -- which
            // may be code Pin attributes to no routine (e.g. symbol-table
            // gaps in the static glibc of sgcc_base/sgcc_peak) and is
            // therefore never translated. In the TC, however, the next
            // bytes belong to the translation of an unrelated routine, so
            // falling through would execute wrong code. Append an explicit
            // "jmp <fall-through address>": the chaining step redirects it
            // to the translated copy of that address when one exists, and
            // otherwise it becomes an indirect jump back to original code.
            {
              INS last = RTN_InsTail(rtn);
              if (INS_Valid(last) && INS_HasFallThrough(last)) {
                ADDRINT last_addr = INS_Address(last);
                ADDRINT fallthru_addr = last_addr + INS_Size(last);

                // Close the BBL with its exec-counter stub first. Nothing
                // is provable past the routine end, so dead_mask = 0 (the
                // stub saves/restores everything it uses).
                if (!KnobNoProfile) {
                  rc = add_profiling_instrs(last, last_addr,
                                            &bbl_map[bbl_num].counter, bbl_num,
                                            0 /* nothing provably dead */);
                  if (rc < 0) {
                    RTN_Close(rtn);
                    return -1;
                  }
                }

                xed_encoder_instruction_t enc_instr;
                xed_inst1(&enc_instr, dstate, XED_ICLASS_JMP, 64,
                          xed_relbr(0, 32));
                if (add_new_encoded_instr(last_addr, &enc_instr, RegularIns) < 0) {
                  RTN_Close(rtn);
                  return -1;
                }
                // The dummy displacement above made the entry derive a
                // bogus target; set the real fall-through target so the
                // chaining/fixup passes redirect it correctly.
                instr_map[num_of_instr_map_entries - 1].orig_targ_addr = fallthru_addr;

                bbl_map[bbl_num].terminating_ins_entry = num_of_instr_map_entries - 1;
                bbl_num++;
                if (bbl_num >= max_bbl_count) {
                  cerr << "out of memory for bbl_map" << endl;
                  RTN_Close(rtn);
                  return -1;
                }
                bbl_map[bbl_num].starting_ins_entry = num_of_instr_map_entries;
              }
            }

            // Close the RTN.
            RTN_Close( rtn );

            // Apply local chaining of direct calls and branches for this routine.
            //chain_all_direct_jmp_and_call_target_entries(rtn_entry, num_of_instr_map_entries);

         } // end for RTN..
    } // end for SEC...

    return 0;
}


/***************************/
/* int copy_instrs_to_tc() */
/***************************/
int copy_instrs_to_tc(char *tc)
{
    int cursor = 0;

    for (unsigned i=0; i < num_of_instr_map_entries; i++) {

      if ((ADDRINT)&tc[cursor] != instr_map[i].new_ins_addr) {
          cerr << "ERROR: Non-matching instruction addresses: "
               << hex << (ADDRINT)&tc[cursor]
               << " vs. " << instr_map[i].new_ins_addr << endl;
          return -1;
      }

      memcpy(&tc[cursor], (char *)instr_map[i].encoded_ins, instr_map[i].size);

      cursor += instr_map[i].size;
    }

    return cursor;
}


/***************************************/
/* void commit_translated_rtns_to_tc() */
/***************************************/
inline void commit_translated_rtns_to_tc()
{
    // Commit the translated routines:
    // Go over the routines and replace the original ones
    // by their new successfully translated ones:

    for (unsigned i=0; i < num_of_instr_map_entries; i++) {

        //replace routine by new routine in tc

        if (instr_map[i].ins_type != RtnHeadIns)
          continue;

        RTN rtn = RTN_FindByAddress(instr_map[i].orig_ins_addr);
        if (rtn == RTN_Invalid()) {
           cerr << "invalid rtN for commit for addr: 0x"
                << instr_map[i].orig_ins_addr << "\n";
           continue;
        }

        // Probe-safety guards: placing a probe on a routine that Pin deems
        // unsafe aborts the whole run, and probing routines shorter than
        // the probe jump itself (< 8 bytes) fails and on some Pin versions
        // corrupts the probe trampolines of neighboring routines. Such
        // routines simply keep running their original code.
        if (!RTN_IsSafeForProbedReplacement(rtn)) {
           cerr << "skipping unsafe-for-probe rtn: " << RTN_Name(rtn) << "\n";
           continue;
        }
        if (RTN_Size(rtn) < 8) {
           cerr << "skipping too-short rtn: " << RTN_Name(rtn) << "\n";
           continue;
        }

        // Debug print.
        // cerr << "committing rtN: " << RTN_Name(rtn);
        // cerr << " from: 0x" << hex << RTN_Address(rtn)
        //      << " to: 0x" << hex << instr_map[i].new_ins_addr << endl;


        AFUNPTR origFptr = RTN_ReplaceProbed(rtn,  (AFUNPTR)instr_map[i].new_ins_addr);

        if (origFptr == NULL) {
            cerr << "RTN_ReplaceProbed failed.";
            cerr << " orig routine addr: 0x" << hex << RTN_Address(rtn)
                 << " translated routine addr: 0x" << hex
                 << instr_map[i].new_ins_addr << endl;
            dump_instr_from_mem ((ADDRINT *)RTN_Address(rtn), RTN_Address(rtn));
        }

        // debug print.
        //if (origFptr != NULL) {
        //  cerr << "RTN_ReplaceProbed succeeded. ";
        //  cerr << " orig routine addr: 0x" << hex << RTN_Address(rtn)
        //       << " translated routine addr: 0x" << hex
        //       << instr_map[i].new_ins_addr << endl;
        //  dump_instr_from_mem ((ADDRINT *)RTN_Address(rtn), RTN_Address(rtn));
        //}
    }
}

/**********************************************/
/* start_stop_profile_gathering_thread_func() */
/**********************************************/
void start_stop_profile_gathering_thread_func(void *v)
{
    // Wait prof_time seconds for the profiling to count
    // execution frequency for each BBL.
    cerr << " prof time: " << dec << KnobNumSecsDuringProfile << " sec\n";
    sleep(KnobNumSecsDuringProfile);

    cerr << "disabling profile gathering\n";

    // Make sure the signal below is not delivered to THIS internal
    // thread (it must run on the application thread).
    sigset_t blk;
    sigemptyset(&blk);
    sigaddset(&blk, SIGUSR2);
    sigprocmask(SIG_BLOCK, &blk, NULL);

    // Prepare the skip-jump patches (no code is modified yet).
    int rc = disable_profiling_in_tc(instr_map, num_of_instr_map_entries);
    if  (rc < 0)
      return;

    // Apply them on the application thread: it cannot be mid-stub while
    // it is inside the signal handler, so the 5-byte writes are safe.
    kill(getpid(), SIGUSR2);

    // Wait for the handler to run; fall back to patching directly if the
    // signal is not serviced (e.g. application blocked in a syscall).
    for (int w = 0; w < 5000 && !patches_applied; w++)
      usleep(1000);
    if (!patches_applied) {
      cerr << "warning: patch signal not serviced; patching directly\n";
      apply_patches_handler(0);
    }
    cerr << "profile gathering disabled (" << dec << num_pending_patches
         << " stubs patched)\n";
}

/****************************/
/* allocate_and_init_memory */
/****************************/
int allocate_and_init_memory(IMG img)
{
    // Calculate size of executable sections and allocate required memory:
    //
    ADDRINT highest_addr = 0;
    for (SEC sec = IMG_SecHead(img); SEC_Valid(sec); sec = SEC_Next(sec))
    {
        if (!SEC_IsExecutable(sec) || SEC_IsWriteable(sec) || !SEC_Address(sec))
            continue;

        if (!lowest_sec_addr || lowest_sec_addr > SEC_Address(sec))
            lowest_sec_addr = SEC_Address(sec);

        if (highest_sec_addr < SEC_Address(sec) + SEC_Size(sec))
            highest_sec_addr = SEC_Address(sec) + SEC_Size(sec);

        // need to avouid using RTN_Open as it is expensive...
        for (RTN rtn = SEC_RtnHead(sec); RTN_Valid(rtn); rtn = RTN_Next(rtn))
        {
            if (highest_addr < RTN_Address(rtn) + RTN_Size(rtn))
                highest_addr = RTN_Address(rtn) + RTN_Size(rtn);
            max_rtn_count++;
            max_ins_count += RTN_NumIns  (rtn);
        }
    }

    // There can be at most one BBL per original instruction; capture the
    // bound before max_ins_count is inflated for the profiling stubs.
    max_bbl_count = max_ins_count + 2;

    max_ins_count *= 10; // estimating that the num of instrs for the profiling
                         // and for the inlined functions will not exceed
                         // the total nunmber of the entire code.


    // get a page size in the system:
    int pagesize = sysconf(_SC_PAGE_SIZE);
    if (pagesize == -1) {
      perror("sysconf");
      return -1;
    }

    ADDRINT text_size = (highest_sec_addr - lowest_sec_addr) * 2 + pagesize * 4;

    max_tc_size = 10 * text_size + pagesize * 4;   // FIXME: need a better estimate
    // Check thet max_tc_size is not larger than a 32 bit branch displacement
    if (max_tc_size >= 0x7FFFFFFF) {
      cerr << "size of TC is beyond the range of a branch displacement" << endl;
      return -1;
    }

    // Allocate the needed memory for tc and tc2 + jump orig addr map +
    // bbl_map with RW+EXEC permissions which is not
    // located in an address that is more than 32bits afar.
    // bbl_map lives here (and not on the heap) so that the profiling stub's
    // RIP-relative "ADD qword ptr [counter], 1" can reach the counters with
    // a 32-bit displacement from the TC.
    const size_t mem_size =
              max_tc_size +                     // TC + TC2 size
              max_rtn_count * sizeof(ADDRINT) + // jump_to_orig_addr_map size
              max_bbl_count * sizeof(bbl_map_t);  // bbl_map size
    if (mem_size >= 0x7FFFFFFF) {
      cerr << "size of TC + bbl_map is beyond the range of a branch displacement" << endl;
      return -1;
    }
    char *addr = nullptr;
    ADDRINT max_distance = 0x7FFFFFFF;
    const size_t step = pagesize; // Try every page
    // Align target address to page boundary.
    // Keep well clear of the brk heap: a non-PIE static binary grows its
    // heap right after its bss, and squatting there makes the program's
    // very first (pre-malloc) sbrk - the TLS block in __libc_setup_tls -
    // fail, which crashes it with a wild memcpy. Aim ~1GB above the image;
    // that still keeps the TC within a 32-bit branch displacement of the
    // original text.
    ADDRINT aligned_target =
        (((ADDRINT)highest_addr) + 0x40000000) & ~((ADDRINT)pagesize - 1);
    // Try exact address first
    void* result = mmap((void*)aligned_target, mem_size,
                       PROT_READ | PROT_WRITE | PROT_EXEC,
                       MAP_PRIVATE | MAP_ANONYMOUS,
                       0, 0);
    if (result != MAP_FAILED &&
        (abs((long)((ADDRINT)result - aligned_target)) <= (long)max_distance)) {
        addr = (char *)result;
    }

    if (!addr) {
        // Search in expanding rings around target
        for (size_t offset = step; offset <= max_distance; offset += step) {
            // Try above target address
            ADDRINT try_addr = aligned_target + offset;
            result = mmap((void*)try_addr, mem_size,
                         PROT_READ | PROT_WRITE | PROT_EXEC,
                         MAP_PRIVATE | MAP_ANONYMOUS,
                         0, 0);
            if (result != MAP_FAILED &&
                (abs((long)((ADDRINT)result - try_addr)) <= (long)max_distance)) {
                addr = (char *)result;
                break;
            }
            if (result != MAP_FAILED) {
                munmap(result, mem_size);
            }

            // Try below target address (if not underflow)
            if (highest_addr >= offset) {
                try_addr = aligned_target - offset;
                result = mmap((void*)try_addr, mem_size,
                             PROT_READ | PROT_WRITE | PROT_EXEC,
                             MAP_PRIVATE | MAP_ANONYMOUS,
                             0, 0);
                if (result != MAP_FAILED &&
                    (abs((long)((ADDRINT)result - try_addr)) <= (long)max_distance)) {
                    addr = (char *)result;
                    break;
                }
                if (result != MAP_FAILED) {
                    munmap(result, mem_size);
                }
            }
        }
    }

    if (!addr) {
        cerr << "failed to allocate memory within 32-bit range. " << endl;
        return -1;
    }

    // debug print.
    cerr << " allocated memory at: 0x" << hex << (ADDRINT)addr << "\n";

    // TC is allocated first.
    tc = (char *)addr;
    addr += max_tc_size;

    // Allocate memory to the jump map to orig addrs which cannot be relocated.
    jump_to_orig_addr_map = (ADDRINT *)addr;
    addr += max_rtn_count * sizeof(ADDRINT);

    // bbl_map sits in the same RIP-reachable region (see mem_size above);
    // the anonymous mmap is already zero-filled.
    bbl_map = (bbl_map_t *)addr;
    if (KnobHeapBbl) {
      // Debug: original placement (heap). The RIP-relative ADD counter
      // optimization then falls back automatically (displacement check).
      bbl_map = (bbl_map_t *)calloc(max_bbl_count, sizeof(bbl_map_t));
      if (!bbl_map) {
        perror("calloc");
        return -1;
      }
    }

    cerr << " region layout: tc=0x" << hex << (ADDRINT)tc
         << " max_tc_size=0x" << max_tc_size
         << " jump_map=0x" << (ADDRINT)jump_to_orig_addr_map
         << " bbl_map=0x" << (ADDRINT)bbl_map
         << " bbl_map_end=0x" << (ADDRINT)(bbl_map + max_bbl_count)
         << " (" << dec << max_bbl_count << " bbls)\n";

    // Allocate memory for the instr_map table. It is tool-internal
    // bookkeeping that translated code never references, so it can live on
    // the ordinary heap.
    instr_map = (instr_map_t *)calloc(max_ins_count, sizeof(instr_map_t));
    if (instr_map == NULL) {
        perror("calloc");
        return -1;
    }

    return 0;
}



/* ============================================ */
/* Main translation routine                     */
/* ============================================ */
typedef VOID (*EXITFUNCPTR)(INT code);
EXITFUNCPTR origExit;

/********/
/* Fini */
/********/
VOID Fini(INT32 code, VOID* v)
{
    cerr << "Reached _exit." << endl;
    dump_profile();

    clock_gettime(CLOCK_MONOTONIC, &end_running_time);
    double elapsed = (end_running_time.tv_sec - start_running_time.tv_sec) + 
                     (end_running_time.tv_nsec - start_running_time.tv_nsec) / 1e9;
	cerr << " Translated code run took: " << elapsed << " seconds\n";
}

/*******************/
/* ExitInProbeMode */
/*******************/
VOID ExitInProbeMode(INT code)
{
    Fini(code, 0);
    (*origExit)(code);
}

/*************/
/* create_tc */
/*************/
VOID create_tc(IMG img, VOID *v)
{
    // Insert a call to function Fini when raching the _exit routine.
    RTN exitRtn = RTN_FindByName(img, "_exit");
    if (RTN_Valid(exitRtn) && RTN_IsSafeForProbedReplacement(exitRtn)) {
      origExit = (EXITFUNCPTR)RTN_ReplaceProbed(exitRtn, AFUNPTR(ExitInProbeMode));
    }

    // Step 0: Check the image and the CPU:
    if (!IMG_IsMainExecutable(img))
      return;

    if (KnobDumpOrigCode)
      dump_image_instrs(img);

    int rc = 0;

    struct timespec start;
    clock_gettime(CLOCK_MONOTONIC, &start);

    // step 1: Check size of executable sections and allocate required memory:
    rc = allocate_and_init_memory(img);
    if (rc < 0) {
        cerr << "failed to initialize memory for translation\n";
        return;
    }
    cerr << "after memory allocation" << endl;

    // Step 2: go over all routines and identify candidate routines and copy
    //         their code into the instr map IR:
    rc = find_candidate_rtns_for_tc(img);
    if (rc < 0) {
        cerr << "failed to find candidates for translation\n";
        return;
    }
    cerr << "after identifying candidate routines" << endl;
    cerr << " dead-reg optimization: " << dec << opt_stats.stubs_total << " stubs, "
         << opt_stats.stubs_mem_add << " use ADD [counter],1, "
         << opt_stats.stubs_rax_skipped << " skip RAX save, "
         << opt_stats.stubs_rbx_skipped << " skip RBX save, "
         << opt_stats.stubs_rcx_skipped << " skip RCX save" << endl;

    // Step 3: Chaining - calculate direct branch and call instructions to point
    //         to corresponding target instr entries:
    chain_all_direct_jmp_and_call_target_entries(0, num_of_instr_map_entries);
    cerr << "after chaining all branch targets" << endl;

    // Step 4: Set initial estimated new addrs for each instruction in the tc.
    rc = set_initial_estimated_new_ins_addrs_in_tc(tc);
    if (rc < 0 ) {
        cerr << "failed to set initial estimated new ins addrs in the TC\n";
        return;
    }
    cerr << "after setting initial estimated new ins addrs in the TC" << endl;

    // Step 5: fix rip-based, direct branch and direct call displacements:
    rc = fix_instructions_displacements();
    if (rc < 0 ) {
        cerr << "failed to fix displacments of translated instructions\n";
        return;
    }
    cerr << "after fixing instructions displacements" << endl;

    // Step 6: write translated instructions to the tc:
    rc = copy_instrs_to_tc(tc);
    if (rc < 0 ) {
        cerr << "failed to copy the instructions to the translation cache\n";
        return;
    }
    tc_size = rc;
    cerr << "after write all new instructions to memory tc" << endl;

    // Verify the committed TC: decode every branch with a known target
    // entry and check that its displacement really lands on that entry's
    // final address. Catches any mis-encoded displacement left by the
    // fixup passes.
    {
      unsigned bad = 0;
      for (unsigned i = 0; i < num_of_instr_map_entries; i++) {
        if (!instr_map[i].size || instr_map[i].targ_map_entry < 0)
          continue;
        xed_decoded_inst_t xedd;
        xed_decoded_inst_zero_set_mode(&xedd, &dstate);
        if (xed_decode(&xedd, reinterpret_cast<UINT8*>(instr_map[i].new_ins_addr),
                       max_inst_len) != XED_ERROR_NONE) {
          cerr << "TCVERIFY: undecodable instr at TC 0x" << hex
               << instr_map[i].new_ins_addr << " (entry " << dec << i << ")\n";
          bad++;
          continue;
        }
        xed_uint_t disp_bits = xed_decoded_inst_get_branch_displacement_width_bits(&xedd);
        if (!disp_bits)
          continue;
        xed_int64_t disp = xed_decoded_inst_get_branch_displacement(&xedd);
        ADDRINT actual = instr_map[i].new_ins_addr +
                         xed_decoded_inst_get_length(&xedd) + disp;
        ADDRINT want = instr_map[instr_map[i].targ_map_entry].new_ins_addr;
        if (actual != want) {
          cerr << "TCVERIFY MISMATCH entry " << dec << i
               << " orig 0x" << hex << instr_map[i].orig_ins_addr
               << " tc 0x" << instr_map[i].new_ins_addr
               << " goes to 0x" << actual
               << " want 0x" << want
               << " (targ orig 0x"
               << instr_map[instr_map[i].targ_map_entry].orig_ins_addr << ")\n";
          if (++bad > 20) break;
        }
      }
      // Also verify every RIP-relative memory operand points where the
      // original intended (data target recorded in orig_rip_addr).
      for (unsigned i = 0; i < num_of_instr_map_entries; i++) {
        if (!instr_map[i].size || !instr_map[i].orig_rip_addr ||
            instr_map[i].orig_targ_addr)
          continue;
        xed_decoded_inst_t xedd;
        xed_decoded_inst_zero_set_mode(&xedd, &dstate);
        if (xed_decode(&xedd, reinterpret_cast<UINT8*>(instr_map[i].new_ins_addr),
                       max_inst_len) != XED_ERROR_NONE)
          continue;
        if (!xed_decoded_inst_number_of_memory_operands(&xedd) ||
            xed_decoded_inst_get_base_reg(&xedd, 0) != XED_REG_RIP)
          continue;
        xed_int64_t mdisp = xed_decoded_inst_get_memory_displacement(&xedd, 0);
        ADDRINT actual = instr_map[i].new_ins_addr +
                         xed_decoded_inst_get_length(&xedd) + mdisp;
        ADDRINT want = instr_map[i].orig_rip_addr;
        if (actual != want) {
          cerr << "TCVERIFY RIP MISMATCH entry " << dec << i
               << " orig 0x" << hex << instr_map[i].orig_ins_addr
               << " tc 0x" << instr_map[i].new_ins_addr
               << " mem target 0x" << actual
               << " want 0x" << want << "\n";
          if (++bad > 20) break;
        }
      }
      cerr << "TCVERIFY: " << dec << bad << " bad branch/rip targets\n";
    }

    // Debug: locate a TC address (e.g. a crash ip) or an orig address in the
    // instr map and dump the surrounding translated instructions.
    if (KnobDumpAround.Value()) {
      ADDRINT want = (ADDRINT)KnobDumpAround.Value();
      for (unsigned i = 0; i < num_of_instr_map_entries; i++) {
        if ((instr_map[i].new_ins_addr <= want &&
             want < instr_map[i].new_ins_addr + instr_map[i].size) ||
            instr_map[i].orig_ins_addr == want) {
          unsigned lo = (i > 40) ? i - 40 : 0;
          unsigned hi = (i + 10 < num_of_instr_map_entries) ? i + 10 : num_of_instr_map_entries;
          for (unsigned j = lo; j < hi; j++) {
            cerr << (j == i ? ">>> " : "    ")
                 << "entry " << dec << j
                 << " type " << instr_map[j].ins_type
                 << " bbl " << instr_map[j].bbl_num
                 << " orig 0x" << hex << instr_map[j].orig_ins_addr << " ";
            dump_instr_from_mem((ADDRINT *)instr_map[j].encoded_ins,
                                instr_map[j].new_ins_addr);
          }
          break;
        }
      }
    }

    if (KnobDumpTranslatedCode) {
       cerr << "Translation Cache dump:" << endl;
       dump_tc(tc, tc_size);  // dump the entire tc

       //cerr << endl << "instructions map dump:" << endl;
       //dump_profile();     // dump all translated instructions in map_instr
    }

    // Step 7: Commit the translated routines:
    //         Go over the candidate functions and replace the original ones
    //         by their new successfully translated ones:
    if (!KnobDoNotCommitTranslatedCode) {
      commit_translated_rtns_to_tc();
      cerr << "after commit of translated routines from orig code to TC" << endl;
    }

    struct timespec end;
    clock_gettime(CLOCK_MONOTONIC, &end);
    double elapsed = (end.tv_sec - start.tv_sec) +
	                 (end.tv_nsec - start.tv_nsec) / 1e9;
    cerr << " create_tc took: " << elapsed << " seconds\n";

	clock_gettime(CLOCK_MONOTONIC, &start_running_time);
}



/* ===================================================================== */
/* Print Help Message                                                    */
/* ===================================================================== */
INT32 Usage()
{
    cerr << "This tool translated routines of an Intel(R) 64 binary"
         << endl;
    cerr << KNOB_BASE::StringKnobSummary();
    cerr << endl;
    return -1;
}


/* ===================================================================== */
/* Main                                                                  */
/* ===================================================================== */

int main(int argc, char * argv[])
{
    // Open output profile file.
    out = new std::ofstream("edge-profile.csv");

    // Initialize pin & symbol manager
    if( PIN_Init(argc,argv) )
        return Usage();

    PIN_InitSymbols();

    if (KnobCatchSegv) {
        struct sigaction sa;
        memset(&sa, 0, sizeof(sa));
        sa.sa_sigaction = segv_report_handler;
        sa.sa_flags = SA_SIGINFO;
        sigaction(SIGSEGV, &sa, NULL);
        sigaction(SIGILL, &sa, NULL);
        sigaction(SIGBUS, &sa, NULL);
    }

    // The disable-profiling patches are applied on the application thread
    // inside this handler (see start_stop_profile_gathering_thread_func).
    {
        struct sigaction sa;
        memset(&sa, 0, sizeof(sa));
        sa.sa_handler = apply_patches_handler;
        sa.sa_flags = SA_RESTART;
        sigaction(SIGUSR2, &sa, NULL);
    }

    // Register create_tc
    IMG_AddInstrumentFunction(create_tc, 0);

    // Create internal thread to start and stop profile gathering.
    THREADID tid = PIN_SpawnInternalThread(start_stop_profile_gathering_thread_func, NULL, 0, NULL);
    if (tid == INVALID_THREADID) {
        cerr << "failed to spawn a thread for commit" << endl;
    }

    // Start the program, never returns
    PIN_StartProgramProbed();

    return 0;
}

/* ===================================================================== */
/* eof */
/* ===================================================================== */

