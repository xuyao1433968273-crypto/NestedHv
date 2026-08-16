#ifndef NHV_VMCS12_H
#define NHV_VMCS12_H

#include <stdint.h>
#include "nhv_vmcs.h"

/*
 * A shadow of L1's virtual VMCS (VMCS12).
 *
 * L1 thinks it owns real hardware VMCS pages; the L0 keeps one NHV_VMCS12_OBJECT
 * per guest-physical VMCS page that L1 touches. VMPTRLD/VMCLEAR/VMREAD/VMWRITE/
 * VMLAUNCH/VMRESUME operate on this shadow exactly like the real instructions
 * operate on a real VMCS, including the VMfail/VM-instruction-error semantics.
 *
 * This module is pure logic: no VMX instructions, no MSRs, fully testable on a
 * normal host.
 */

#define NHV_VMCS12_MAX_FIELDS  256u
#define NHV_VMCS12_MAX_OBJECTS 8u

#define NHV_VMCS12_NO_CURRENT_INDEX (-1)

#define NHV_VMCS12_LAUNCH_CLEAR    0u
#define NHV_VMCS12_LAUNCH_LAUNCHED 1u

/* SDM VM-instruction error codes modeled by this module. */
#define NHV_VMERR_UNSUPPORTED              1u
#define NHV_VMERR_WRITE_TO_READONLY        12u

typedef struct NHV_VMCS_FIELD {
    uint32_t encoding;
    uint64_t value;
    uint8_t  present;
    uint8_t  reserved[7];
} NHV_VMCS_FIELD;

typedef struct NHV_VMCS12_OBJECT {
    uint64_t        guest_phys_addr;
    uint32_t        field_count;
    uint32_t        launch_state; /* NHV_VMCS12_LAUNCH_* */
    NHV_VMCS_FIELD  fields[NHV_VMCS12_MAX_FIELDS];
} NHV_VMCS12_OBJECT;

typedef struct NHV_VMCS12_STORE {
    NHV_VMCS12_OBJECT objects[NHV_VMCS12_MAX_OBJECTS];
    uint32_t          object_count;
    int32_t           current_index; /* NHV_VMCS12_NO_CURRENT_INDEX when none */
} NHV_VMCS12_STORE;

typedef enum NHV_RESULT_KIND {
    NHV_OK = 0,
    NHV_VMFAIL_INVALID = 1, /* CF=1, ZF=0 */
    NHV_VMFAIL_VALID   = 2  /* CF=1, ZF=1 */
} NHV_RESULT_KIND;

typedef struct NHV_RESULT {
    NHV_RESULT_KIND kind;
    uint32_t        vm_instr_error;
} NHV_RESULT;

void nhv_vmcs12_store_init(NHV_VMCS12_STORE* store);

/* VMPTRLD: make the object for `gpa` current (creating it if needed). */
NHV_RESULT nhv_vmcs12_vmptrld(NHV_VMCS12_STORE* store, uint64_t gpa);

/* VMCLEAR: mark the object for `gpa` clear. */
NHV_RESULT nhv_vmcs12_vmclear(NHV_VMCS12_STORE* store, uint64_t gpa);

/* VMPTRST: return the current object's GPA. Fails when there is no current. */
NHV_RESULT nhv_vmcs12_vmptrst(const NHV_VMCS12_STORE* store, uint64_t* gpa);

/* VMREAD: read `enc` from the current object. */
NHV_RESULT nhv_vmcs12_vmread(const NHV_VMCS12_STORE* store, uint32_t enc, uint64_t* value);

/* VMWRITE: write `value` to `enc` in the current object. */
NHV_RESULT nhv_vmcs12_vmwrite(NHV_VMCS12_STORE* store, uint32_t enc, uint64_t value);

/* L0-internal raw write: like VMWRITE but bypasses the read-only access check.
 * The L0 uses this to synthesize a reflected VM-exit into VMCS12, writing the
 * exit-reason/qualification fields that L1 itself may not write. */
NHV_RESULT nhv_vmcs12_write_raw(NHV_VMCS12_STORE* store, uint32_t enc, uint64_t value);

/* Launch-state transitions (used by VMLAUNCH/VMRESUME emulation). */
NHV_RESULT nhv_vmcs12_begin_launch(NHV_VMCS12_STORE* store);
NHV_RESULT nhv_vmcs12_begin_resume(NHV_VMCS12_STORE* store);
uint32_t   nhv_vmcs12_launch_state(const NHV_VMCS12_STORE* store);

/* Access to the current object (read-only). */
const NHV_VMCS12_OBJECT* nhv_vmcs12_current(const NHV_VMCS12_STORE* store);

#endif /* NHV_VMCS12_H */
