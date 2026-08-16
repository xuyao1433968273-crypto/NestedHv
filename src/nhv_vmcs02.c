#include "nhv_vmcs02.h"

/* Read a field value from a VMCS12 object. Returns 0 when the field is absent
 * (matching VMREAD-before-VMWRITE returning zero). */
static uint64_t read_field(const NHV_VMCS12_OBJECT* obj, uint32_t enc)
{
    uint32_t i;
    if (obj == 0) {
        return 0;
    }
    for (i = 0; i < obj->field_count; ++i) {
        if (obj->fields[i].encoding == enc && obj->fields[i].present) {
            return obj->fields[i].value;
        }
    }
    return 0;
}

static NHV_RESULT ok_result(void)
{
    NHV_RESULT r;
    r.kind = NHV_OK;
    r.vm_instr_error = 0;
    return r;
}

static NHV_RESULT fail_result(uint32_t err)
{
    NHV_RESULT r;
    r.kind = NHV_VMFAIL_VALID;
    r.vm_instr_error = err;
    return r;
}

/* Derive the fixed-bit masks from the allowed masks:
 *   must0 = bits that MUST be 0  = ~allowed1
 *   must1 = bits that MUST be 1  = ~allowed0 */
static uint32_t must_be_0(uint32_t allowed1) { return ~allowed1; }
static uint32_t must_be_1(uint32_t allowed0) { return ~allowed0; }

/* A control value is representable iff:
 *   - no bit that must be 0 is set, and
 *   - every bit that must be 1 is set. */
static int control_is_valid(uint32_t v, uint32_t allowed1, uint32_t allowed0)
{
    uint32_t must0 = must_be_0(allowed1);
    uint32_t must1 = must_be_1(allowed0);
    return ((v & must0) == 0) && ((v & must1) == must1);
}

/* final = (requested | must1) & ~must0 */
static uint32_t merge_control(uint32_t requested, uint32_t must1, uint32_t must0)
{
    return (requested | must1) & ~must0;
}

#define NHV_ACTIVATE_SECONDARY 0x80000000u /* primary-controls bit 31 */

NHV_RESULT nhv_vmcs02_build(const NHV_VMCS12_OBJECT* vmcs12,
                            const NHV_VMCS_CAPS* caps,
                            const NHV_L0_CONTROL_REQS* l0_reqs,
                            const NHV_L0_HOST_STATE* l0_host,
                            NHV_VMCS02_PLAN* out)
{
    uint32_t l1_pin, l1_proc, l1_sec, l1_exit, l1_entry;
    int l1_wants_secondary;

    if (vmcs12 == 0 || caps == 0 || l0_reqs == 0 || l0_host == 0 || out == 0) {
        return fail_result(NHV_VMERR_ENTRY_INVALID_CONTROL);
    }

    l1_pin   = (uint32_t)read_field(vmcs12, NHV_PIN_CONTROLS);
    l1_proc  = (uint32_t)read_field(vmcs12, NHV_PRIMARY_CONTROLS);
    l1_exit  = (uint32_t)read_field(vmcs12, NHV_EXIT_CONTROLS);
    l1_entry = (uint32_t)read_field(vmcs12, NHV_ENTRY_CONTROLS);
    l1_sec   = (uint32_t)read_field(vmcs12, NHV_SECONDARY_CONTROLS);

    l1_wants_secondary = (l1_proc & NHV_ACTIVATE_SECONDARY) != 0;

    /* --- 1. Validate L1's request against what the hardware can do. ----------
     * A bit L1 set/cleared that the hardware cannot honor makes its
     * VMLAUNCH/VMRESUME fail with VM-entry error 7. */
    if (!control_is_valid(l1_pin, caps->pin_allowed1, caps->pin_allowed0)) {
        return fail_result(NHV_VMERR_ENTRY_INVALID_CONTROL);
    }
    if (!control_is_valid(l1_proc, caps->proc_allowed1, caps->proc_allowed0)) {
        return fail_result(NHV_VMERR_ENTRY_INVALID_CONTROL);
    }
    if (!control_is_valid(l1_exit, caps->exit_allowed1, caps->exit_allowed0)) {
        return fail_result(NHV_VMERR_ENTRY_INVALID_CONTROL);
    }
    if (!control_is_valid(l1_entry, caps->entry_allowed1, caps->entry_allowed0)) {
        return fail_result(NHV_VMERR_ENTRY_INVALID_CONTROL);
    }
    if (l1_wants_secondary) {
        if (!caps->secondary_supported) {
            return fail_result(NHV_VMERR_ENTRY_INVALID_CONTROL);
        }
        if (!control_is_valid(l1_sec, caps->sec_allowed1, caps->sec_allowed0)) {
            return fail_result(NHV_VMERR_ENTRY_INVALID_CONTROL);
        }
    }

    /* --- 2. Merge L0's must-set / must-clear bits. -------------------------- */
    out->pin_controls = merge_control(
        l1_pin, l0_reqs->pin_must1, l0_reqs->pin_must0);
    out->primary_controls = merge_control(
        l1_proc, l0_reqs->proc_must1, l0_reqs->proc_must0);
    out->exit_controls = merge_control(
        l1_exit, l0_reqs->exit_must1, l0_reqs->exit_must0);
    out->entry_controls = merge_control(
        l1_entry, l0_reqs->entry_must1, l0_reqs->entry_must0);

    out->secondary_active =
        (out->primary_controls & NHV_ACTIVATE_SECONDARY) != 0;
    if (out->secondary_active) {
        out->secondary_controls = merge_control(
            l1_sec, l0_reqs->sec_must1, l0_reqs->sec_must0);
    } else {
        out->secondary_controls = 0;
    }

    /* --- 3. Defensive: the final VMCS02 must itself be valid on this hardware.
     * This can only trip if L0's own must1/must0 requirements contradict the
     * capability masks (an L0 bug, not an L1 bug). */
    if (!control_is_valid(out->pin_controls, caps->pin_allowed1, caps->pin_allowed0) ||
        !control_is_valid(out->primary_controls, caps->proc_allowed1, caps->proc_allowed0) ||
        !control_is_valid(out->exit_controls, caps->exit_allowed1, caps->exit_allowed0) ||
        !control_is_valid(out->entry_controls, caps->entry_allowed1, caps->entry_allowed0)) {
        return fail_result(NHV_VMERR_ENTRY_INVALID_CONTROL);
    }
    if (out->secondary_active &&
        !control_is_valid(out->secondary_controls, caps->sec_allowed1, caps->sec_allowed0)) {
        return fail_result(NHV_VMERR_ENTRY_INVALID_CONTROL);
    }

    /* --- 4. Guest (L2) state: copied verbatim from VMCS12. ------------------ */
    out->guest_rip     = read_field(vmcs12, NHV_GUEST_RIP);
    out->guest_rsp     = read_field(vmcs12, NHV_GUEST_RSP);
    out->guest_cr0     = read_field(vmcs12, NHV_GUEST_CR0);
    out->guest_cr3     = read_field(vmcs12, NHV_GUEST_CR3);
    out->guest_cr4     = read_field(vmcs12, NHV_GUEST_CR4);
    out->guest_efer    = read_field(vmcs12, NHV_GUEST_IA32_EFER);
    out->guest_cs_sel  = (uint16_t)read_field(vmcs12, NHV_GUEST_CS_SELECTOR);
    out->guest_ss_sel  = (uint16_t)read_field(vmcs12, NHV_GUEST_SS_SELECTOR);
    out->guest_cs_base = read_field(vmcs12, NHV_GUEST_CS_BASE);
    out->guest_ss_base = read_field(vmcs12, NHV_GUEST_SS_BASE);

    /* --- 5. Host state: always L0's, never L1's. ---------------------------- */
    out->host = *l0_host;

    return ok_result();
}
