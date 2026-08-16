#ifndef NHV_VMCS02_H
#define NHV_VMCS02_H

#include <stdint.h>
#include "nhv_vmcs.h"
#include "nhv_vmcs12.h"

/*
 * VMCS02 build - the software merge step of nested virtualization.
 *
 * L1 owns a *virtual* VMCS (VMCS12, our shadow object). When L1 executes
 * VMLAUNCH or VMRESUME, L0 must turn that into a real hardware VMCS (VMCS02)
 * and launch L2 from it.
 *
 * The merge follows the nested-VMX model in the Intel SDM (Vol. 3, VMCS
 * shadowing / software virtualization of VMX):
 *
 *   - Control fields:   VMCS02 = (L1 requested | L0 must-set) & ~L0 must-clear,
 *                       validated against what the hardware can actually do.
 *   - Guest-state:      copied verbatim from VMCS12 (L2's state belongs to L2).
 *   - Host-state:       supplied by L0, never taken from VMCS12 (after a VM exit
 *                       control returns to L0, not to L1's "host").
 *
 * This module is pure logic - no VMX instructions, no MSRs - fully testable on
 * a normal host.
 */

/*
 * Hardware capability masks. These are the "allowed 1-settings" (bits 31:0)
 * and "allowed 0-settings" (bits 63:32) reported by the VMX control MSRs:
 *
 *   IA32_VMX_TRUE_PIN_BASED_CTLS     (use TRUE MSR when IA32_VMX_BASIC[55]=1)
 *   IA32_VMX_TRUE_PROCBASED_CTLS
 *   IA32_VMX_PROCBASED_CTLS2         (secondary; no separate TRUE MSR)
 *   IA32_VMX_TRUE_EXIT_CTLS
 *   IA32_VMX_TRUE_ENTRY_CTLS
 *
 *   allowed1: bits that MAY be 1.  A bit clear here MUST be 0.
 *   allowed0: bits that MAY be 0.  A bit clear here MUST be 1.
 */
typedef struct NHV_VMCS_CAPS {
    uint32_t pin_allowed1,   pin_allowed0;
    uint32_t proc_allowed1,  proc_allowed0;
    uint32_t sec_allowed1,   sec_allowed0;
    uint32_t exit_allowed1,  exit_allowed0;
    uint32_t entry_allowed1, entry_allowed0;
    uint8_t  secondary_supported; /* 1 when IA32_VMX_PROCBASED_CTLS2 is usable */
} NHV_VMCS_CAPS;

/*
 * L0's own control requirements: bits L0 must force on (must1) or off (must0)
 * in VMCS02 to virtualize L1 regardless of what L1 requested.
 */
typedef struct NHV_L0_CONTROL_REQS {
    uint32_t pin_must1,   pin_must0;
    uint32_t proc_must1,  proc_must0;
    uint32_t sec_must1,   sec_must0;
    uint32_t exit_must1,  exit_must0;
    uint32_t entry_must1, entry_must0;
} NHV_L0_CONTROL_REQS;

/* L0's own host state, loaded into the VMCS02 host-state area. */
typedef struct NHV_L0_HOST_STATE {
    uint64_t rip, rsp;
    uint64_t cr0, cr3, cr4;
    uint64_t efer;
} NHV_L0_HOST_STATE;

/* The produced VMCS02 (what L0 would program into real hardware). */
typedef struct NHV_VMCS02_PLAN {
    /* Control fields (merged). */
    uint32_t pin_controls;
    uint32_t primary_controls;
    uint32_t secondary_controls;
    uint32_t exit_controls;
    uint32_t entry_controls;
    uint8_t  secondary_active; /* 1 when the secondary-controls field is used */

    /* Guest (L2) state, copied from VMCS12. */
    uint64_t guest_rip, guest_rsp;
    uint64_t guest_cr0, guest_cr3, guest_cr4;
    uint64_t guest_efer;
    uint16_t guest_cs_sel, guest_ss_sel;
    uint64_t guest_cs_base, guest_ss_base;

    /* Host (L0) state. */
    NHV_L0_HOST_STATE host;
} NHV_VMCS02_PLAN;

/* VM-instruction error L1 sees from VMLAUNCH/VMRESUME when its VMCS12 control
 * fields are not representable on this hardware (SDM error 7). */
#define NHV_VMERR_ENTRY_INVALID_CONTROL 7u

/*
 * Build VMCS02 from L1's VMCS12, L0's control requirements and L0's host state,
 * validating L1's request against the hardware capability masks.
 *
 * Returns NHV_OK and fills *out on success.
 * Returns NHV_VMFAIL_VALID with vm_instr_error = NHV_VMERR_ENTRY_INVALID_CONTROL
 * when L1 requested a control setting the hardware cannot honor.
 */
NHV_RESULT nhv_vmcs02_build(const NHV_VMCS12_OBJECT* vmcs12,
                            const NHV_VMCS_CAPS* caps,
                            const NHV_L0_CONTROL_REQS* l0_reqs,
                            const NHV_L0_HOST_STATE* l0_host,
                            NHV_VMCS02_PLAN* out);

#endif /* NHV_VMCS02_H */
