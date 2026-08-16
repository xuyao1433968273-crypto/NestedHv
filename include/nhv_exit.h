#ifndef NHV_EXIT_H
#define NHV_EXIT_H

#include <stdint.h>
#include "nhv_vmcs.h"
#include "nhv_vmcs12.h"

/*
 * VM-exit classification and reflection (nested VMX).
 *
 * When L2 causes a VM-exit to L0, L0 must decide what to do with it:
 *
 *   - REFLECT:    L1 asked to intercept this event (it set the matching
 *                 VM-execution control in its VMCS12), so L0 synthesizes a
 *                 VM-exit in L1 and lets L1's handler run.
 *   - L0_HANDLE:  the exit happened for L0's own reasons (L0 forced the
 *                 control, or the instruction exits unconditionally), so L0
 *                 emulates the event itself.
 *   - UNSUPPORTED: an event neither side can safely own (triple fault, VM-entry
 *                 failure); L0 fails closed.
 *
 * The classification reads L1's *requested* controls from VMCS12, not the
 * merged VMCS02 - that is exactly what determines whether L1 wanted the exit.
 *
 * This module is pure logic and fully testable on a normal host. Bit-level
 * lookups that need the I/O or MSR bitmap *contents* (guest-physical pages) are
 * left to the caller, which passes the already-decoded bit in NHV_EXIT_INFO.
 */

/* Basic exit reasons (exit-reason field bits 15:0), SDM Vol. 3C Table 28-1. */
#define NHV_EXIT_EXCEPTION_NMI        0u
#define NHV_EXIT_EXTERNAL_INTERRUPT   1u
#define NHV_EXIT_TRIPLE_FAULT         2u
#define NHV_EXIT_CPUID                10u
#define NHV_EXIT_HLT                  12u
#define NHV_EXIT_INVD                 13u
#define NHV_EXIT_RDTSC                16u
#define NHV_EXIT_VMCALL               18u
#define NHV_EXIT_VMCLEAR              19u
#define NHV_EXIT_VMLAUNCH             20u
#define NHV_EXIT_VMPTRLD              21u
#define NHV_EXIT_VMPTRST              22u
#define NHV_EXIT_VMREAD               23u
#define NHV_EXIT_VMWRITE              24u
#define NHV_EXIT_VMXOFF               25u
#define NHV_EXIT_VMXON                26u
#define NHV_EXIT_CR_ACCESS            28u
#define NHV_EXIT_MOV_DR               29u
#define NHV_EXIT_IO_INSTR             30u
#define NHV_EXIT_RDMSR                31u
#define NHV_EXIT_WRMSR                32u
#define NHV_EXIT_ENTRY_FAILURE        33u
#define NHV_EXIT_MWAIT                36u
#define NHV_EXIT_MONITOR              39u
#define NHV_EXIT_PAUSE                41u
#define NHV_EXIT_WBINVD               54u
#define NHV_EXIT_XSETBV               55u
#define NHV_EXIT_RDRAND               57u
#define NHV_EXIT_INVPCID              58u
#define NHV_EXIT_RDSEED               61u
#define NHV_EXIT_XSAVES               63u
#define NHV_EXIT_XRSTORS              64u
#define NHV_EXIT_UMWAIT               68u
#define NHV_EXIT_TPAUSE               69u

/* Pin-based VM-execution controls. */
#define NHV_PIN_EXT_INTR_EXITING   (1u << 0)
#define NHV_PIN_NMI_EXITING        (1u << 3)

/* Primary processor-based VM-execution controls. */
#define NHV_PROC_HLT_EXITING       (1u << 7)
#define NHV_PROC_RDTSC_EXITING     (1u << 12)
#define NHV_PROC_CR3_LOAD_EXITING  (1u << 15)
#define NHV_PROC_CR3_STORE_EXITING (1u << 16)
#define NHV_PROC_CR8_LOAD_EXITING  (1u << 19)
#define NHV_PROC_CR8_STORE_EXITING (1u << 20)
#define NHV_PROC_MOV_DR_EXITING    (1u << 23)
#define NHV_PROC_UNCOND_IO_EXITING (1u << 24)
#define NHV_PROC_USE_IO_BITMAPS    (1u << 25)
#define NHV_PROC_USE_MSR_BITMAPS   (1u << 28)
#define NHV_PROC_MONITOR_EXITING   (1u << 29)
#define NHV_PROC_PAUSE_EXITING     (1u << 30)

/* Secondary processor-based VM-execution controls. */
#define NHV_PROC2_WBINVD_EXITING      (1u << 6)
#define NHV_PROC2_RDRAND_EXITING      (1u << 11)
#define NHV_PROC2_ENABLE_INVPCID      (1u << 12)
#define NHV_PROC2_RDSEED_EXITING      (1u << 16)
#define NHV_PROC2_ENABLE_XSAVES       (1u << 20)
#define NHV_PROC2_ENABLE_UMWAIT_TPAUSE (1u << 25)

/* CR-access exit qualification (bits 3:0 = CR number, bit 4 = 0 load / 1 store). */
#define NHV_CR_QUAL_NUMBER_MASK  0x0Fu
#define NHV_CR_QUAL_ACCESS_STORE (1u << 4)
#define NHV_CR_NUMBER_CR0 0u
#define NHV_CR_NUMBER_CR3 3u
#define NHV_CR_NUMBER_CR4 4u
#define NHV_CR_NUMBER_CR8 8u

typedef enum NHV_EXIT_DISPOSITION {
    NHV_EXIT_L0_HANDLE = 0, /* L0 owns/emulates; do not reflect */
    NHV_EXIT_REFLECT   = 1, /* synthesize a VM-exit in L1 */
    NHV_EXIT_UNSUPPORTED = 2 /* neither side owns it; fail closed */
} NHV_EXIT_DISPOSITION;

/* L1's requested controls, read from VMCS12 before the VMCS02 merge. */
typedef struct NHV_L1_CONTROLS {
    uint32_t pin;
    uint32_t proc;
    uint32_t proc2;
    uint32_t exception_bitmap;
} NHV_L1_CONTROLS;

/* Details of the exit to classify. */
typedef struct NHV_EXIT_INFO {
    uint32_t basic_reason;
    uint32_t exception_vector; /* when basic_reason == EXCEPTION_NMI (2 = NMI) */
    uint32_t qualification;    /* used for CR-access decoding */
    uint8_t  io_bitmap_bit;    /* decoded L1 I/O bitmap bit (1 = L1 wants it) */
    uint8_t  msr_bitmap_bit;   /* decoded L1 MSR bitmap bit (1 = L1 wants it) */
} NHV_EXIT_INFO;

/* Classify an exit against L1's requested controls. */
NHV_EXIT_DISPOSITION nhv_exit_classify(const NHV_EXIT_INFO* info,
                                       const NHV_L1_CONTROLS* l1);

/* What L0 writes into VMCS12 when it reflects an exit to L1. */
typedef struct NHV_REFLECT_INFO {
    uint32_t exit_reason;
    uint32_t qualification;
    uint64_t guest_rip;
    uint64_t guest_rsp;
    uint64_t guest_rflags;
} NHV_REFLECT_INFO;

/*
 * Synthesize a reflected VM-exit into the current VMCS12: writes the exit
 * reason/qualification and a snapshot of L2's RIP/RSP/RFLAGS.
 *
 * Returns NHV_OK on success, NHV_VMFAIL_INVALID when there is no current VMCS.
 */
NHV_RESULT nhv_exit_reflect(NHV_VMCS12_STORE* store, const NHV_REFLECT_INFO* info);

#endif /* NHV_EXIT_H */
