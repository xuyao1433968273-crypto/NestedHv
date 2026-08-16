#include "nhv_vmcs02.h"

#include <stdio.h>

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (cond) {                                                        \
            ++g_pass;                                                      \
        } else {                                                           \
            ++g_fail;                                                      \
            printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);        \
        }                                                                  \
    } while (0)

/* A fully-flexible hardware: every bit may be 0 or 1, secondary supported. */
static void caps_flexible(NHV_VMCS_CAPS* caps)
{
    caps->pin_allowed1   = 0xFFFFFFFFu;
    caps->pin_allowed0   = 0xFFFFFFFFu;
    caps->proc_allowed1  = 0xFFFFFFFFu;
    caps->proc_allowed0  = 0xFFFFFFFFu;
    caps->sec_allowed1   = 0xFFFFFFFFu;
    caps->sec_allowed0   = 0xFFFFFFFFu;
    caps->exit_allowed1  = 0xFFFFFFFFu;
    caps->exit_allowed0  = 0xFFFFFFFFu;
    caps->entry_allowed1 = 0xFFFFFFFFu;
    caps->entry_allowed0 = 0xFFFFFFFFu;
    caps->secondary_supported = 1;
}

static void reqs_zero(NHV_L0_CONTROL_REQS* r)
{
    r->pin_must1   = 0; r->pin_must0   = 0;
    r->proc_must1  = 0; r->proc_must0  = 0;
    r->sec_must1   = 0; r->sec_must0   = 0;
    r->exit_must1  = 0; r->exit_must0  = 0;
    r->entry_must1 = 0; r->entry_must0 = 0;
}

static void host_default(NHV_L0_HOST_STATE* h)
{
    h->rip  = 0x1000ull;
    h->rsp  = 0x2000ull;
    h->cr0  = 0x3000ull;
    h->cr3  = 0x4000ull;
    h->cr4  = 0x5000ull;
    h->efer = 0x6000ull;
}

/* Build a VMCS12 with control fields and return its current object. */
static const NHV_VMCS12_OBJECT* make_vmcs12(NHV_VMCS12_STORE* store,
                                            uint32_t pin, uint32_t proc,
                                            uint32_t sec, uint32_t exit_,
                                            uint32_t entry)
{
    nhv_vmcs12_store_init(store);
    nhv_vmcs12_vmptrld(store, 0x1000ull);
    nhv_vmcs12_vmwrite(store, NHV_PIN_CONTROLS, pin);
    nhv_vmcs12_vmwrite(store, NHV_PRIMARY_CONTROLS, proc);
    nhv_vmcs12_vmwrite(store, NHV_SECONDARY_CONTROLS, sec);
    nhv_vmcs12_vmwrite(store, NHV_EXIT_CONTROLS, exit_);
    nhv_vmcs12_vmwrite(store, NHV_ENTRY_CONTROLS, entry);
    return nhv_vmcs12_current(store);
}

static void test_basic_merge(void)
{
    NHV_VMCS_CAPS caps;
    NHV_L0_CONTROL_REQS reqs;
    NHV_L0_HOST_STATE host;
    NHV_VMCS12_STORE store;
    NHV_VMCS02_PLAN plan;
    NHV_RESULT r;

    caps_flexible(&caps);
    reqs_zero(&reqs);
    host_default(&host);

    /* L1 wants pin=0x0A; L0 forces bit 2 on, bit 0 off. */
    reqs.pin_must1 = 0x04u;
    reqs.pin_must0 = 0x01u;

    r = nhv_vmcs02_build(make_vmcs12(&store, 0x0Au, 0, 0, 0, 0),
                         &caps, &reqs, &host, &plan);
    CHECK(r.kind == NHV_OK);
    CHECK(plan.pin_controls == 0x0Eu); /* (0x0A|0x04)&~0x01 */

    /* Bits L0 does not touch pass straight through. */
    CHECK(plan.primary_controls == 0u);
    CHECK(plan.exit_controls == 0u);
    CHECK(plan.entry_controls == 0u);
}

static void test_l1_sets_must_be_zero_bit(void)
{
    NHV_VMCS_CAPS caps;
    NHV_L0_CONTROL_REQS reqs;
    NHV_L0_HOST_STATE host;
    NHV_VMCS12_STORE store;
    NHV_VMCS02_PLAN plan;
    NHV_RESULT r;

    caps_flexible(&caps);
    reqs_zero(&reqs);
    host_default(&host);

    /* Hardware cannot set pin bit 0 (allowed1 bit 0 = 0). */
    caps.pin_allowed1 = 0xFFFFFFFEu;

    r = nhv_vmcs02_build(make_vmcs12(&store, 0x01u, 0, 0, 0, 0),
                         &caps, &reqs, &host, &plan);
    CHECK(r.kind == NHV_VMFAIL_VALID);
    CHECK(r.vm_instr_error == NHV_VMERR_ENTRY_INVALID_CONTROL);
}

static void test_l1_clears_must_be_one_bit(void)
{
    NHV_VMCS_CAPS caps;
    NHV_L0_CONTROL_REQS reqs;
    NHV_L0_HOST_STATE host;
    NHV_VMCS12_STORE store;
    NHV_VMCS02_PLAN plan;
    NHV_RESULT r;

    caps_flexible(&caps);
    reqs_zero(&reqs);
    host_default(&host);

    /* Hardware forces pin bit 0 = 1 (allowed0 bit 0 = 0). */
    caps.pin_allowed0 = 0xFFFFFFFEu;

    r = nhv_vmcs02_build(make_vmcs12(&store, 0x00u, 0, 0, 0, 0),
                         &caps, &reqs, &host, &plan);
    CHECK(r.kind == NHV_VMFAIL_VALID);
    CHECK(r.vm_instr_error == NHV_VMERR_ENTRY_INVALID_CONTROL);

    /* Same hardware, but L1 sets bit 0: valid, and it stays set. */
    r = nhv_vmcs02_build(make_vmcs12(&store, 0x01u, 0, 0, 0, 0),
                         &caps, &reqs, &host, &plan);
    CHECK(r.kind == NHV_OK);
    CHECK((plan.pin_controls & 0x01u) == 0x01u);
}

static void test_secondary_controls_merge(void)
{
    NHV_VMCS_CAPS caps;
    NHV_L0_CONTROL_REQS reqs;
    NHV_L0_HOST_STATE host;
    NHV_VMCS12_STORE store;
    NHV_VMCS02_PLAN plan;
    NHV_RESULT r;

    caps_flexible(&caps);
    reqs_zero(&reqs);
    host_default(&host);

    reqs.sec_must1 = 0x10u;
    reqs.sec_must0 = 0x02u;

    r = nhv_vmcs02_build(make_vmcs12(&store, 0, 0x80000000u, 0x04u, 0, 0),
                         &caps, &reqs, &host, &plan);
    CHECK(r.kind == NHV_OK);
    CHECK(plan.secondary_active == 1);
    CHECK(plan.secondary_controls == 0x14u); /* (0x04|0x10)&~0x02 */

    /* Without the activate-secondary bit, the secondary field is ignored. */
    r = nhv_vmcs02_build(make_vmcs12(&store, 0, 0x00000000u, 0x04u, 0, 0),
                         &caps, &reqs, &host, &plan);
    CHECK(r.kind == NHV_OK);
    CHECK(plan.secondary_active == 0);
    CHECK(plan.secondary_controls == 0u);
}

static void test_secondary_unsupported(void)
{
    NHV_VMCS_CAPS caps;
    NHV_L0_CONTROL_REQS reqs;
    NHV_L0_HOST_STATE host;
    NHV_VMCS12_STORE store;
    NHV_VMCS02_PLAN plan;
    NHV_RESULT r;

    caps_flexible(&caps);
    reqs_zero(&reqs);
    host_default(&host);
    caps.secondary_supported = 0;

    r = nhv_vmcs02_build(make_vmcs12(&store, 0, 0x80000000u, 0, 0, 0),
                         &caps, &reqs, &host, &plan);
    CHECK(r.kind == NHV_VMFAIL_VALID);
    CHECK(r.vm_instr_error == NHV_VMERR_ENTRY_INVALID_CONTROL);
}

static void test_l1_secondary_bit_invalid(void)
{
    NHV_VMCS_CAPS caps;
    NHV_L0_CONTROL_REQS reqs;
    NHV_L0_HOST_STATE host;
    NHV_VMCS12_STORE store;
    NHV_VMCS02_PLAN plan;
    NHV_RESULT r;

    caps_flexible(&caps);
    reqs_zero(&reqs);
    host_default(&host);

    /* Hardware cannot set secondary bit 0. */
    caps.sec_allowed1 = 0xFFFFFFFEu;

    r = nhv_vmcs02_build(make_vmcs12(&store, 0, 0x80000000u, 0x01u, 0, 0),
                         &caps, &reqs, &host, &plan);
    CHECK(r.kind == NHV_VMFAIL_VALID);
    CHECK(r.vm_instr_error == NHV_VMERR_ENTRY_INVALID_CONTROL);
}

static void test_guest_state_passthrough(void)
{
    NHV_VMCS_CAPS caps;
    NHV_L0_CONTROL_REQS reqs;
    NHV_L0_HOST_STATE host;
    NHV_VMCS12_STORE store;
    NHV_VMCS02_PLAN plan;
    NHV_RESULT r;

    caps_flexible(&caps);
    reqs_zero(&reqs);
    host_default(&host);

    nhv_vmcs12_store_init(&store);
    nhv_vmcs12_vmptrld(&store, 0x1000ull);
    nhv_vmcs12_vmwrite(&store, NHV_GUEST_RIP,        0x1111ull);
    nhv_vmcs12_vmwrite(&store, NHV_GUEST_RSP,        0x2222ull);
    nhv_vmcs12_vmwrite(&store, NHV_GUEST_CR0,        0x3333ull);
    nhv_vmcs12_vmwrite(&store, NHV_GUEST_CR3,        0x4444ull);
    nhv_vmcs12_vmwrite(&store, NHV_GUEST_CR4,        0x5555ull);
    nhv_vmcs12_vmwrite(&store, NHV_GUEST_IA32_EFER,  0x6666ull);
    nhv_vmcs12_vmwrite(&store, NHV_GUEST_CS_SELECTOR, 0x0018ull);
    nhv_vmcs12_vmwrite(&store, NHV_GUEST_SS_SELECTOR, 0x0020ull);
    nhv_vmcs12_vmwrite(&store, NHV_GUEST_CS_BASE,    0x7777ull);
    nhv_vmcs12_vmwrite(&store, NHV_GUEST_SS_BASE,    0x8888ull);

    r = nhv_vmcs02_build(nhv_vmcs12_current(&store), &caps, &reqs, &host, &plan);
    CHECK(r.kind == NHV_OK);
    CHECK(plan.guest_rip     == 0x1111ull);
    CHECK(plan.guest_rsp     == 0x2222ull);
    CHECK(plan.guest_cr0     == 0x3333ull);
    CHECK(plan.guest_cr3     == 0x4444ull);
    CHECK(plan.guest_cr4     == 0x5555ull);
    CHECK(plan.guest_efer    == 0x6666ull);
    CHECK(plan.guest_cs_sel  == 0x0018u);
    CHECK(plan.guest_ss_sel  == 0x0020u);
    CHECK(plan.guest_cs_base == 0x7777ull);
    CHECK(plan.guest_ss_base == 0x8888ull);
}

static void test_host_state_from_l0(void)
{
    NHV_VMCS_CAPS caps;
    NHV_L0_CONTROL_REQS reqs;
    NHV_L0_HOST_STATE host;
    NHV_VMCS12_STORE store;
    NHV_VMCS02_PLAN plan;
    NHV_RESULT r;

    caps_flexible(&caps);
    reqs_zero(&reqs);
    host_default(&host);

    /* L1 (mistakenly or maliciously) writes host fields into VMCS12.
     * L0 must ignore them and use its own host state. */
    nhv_vmcs12_store_init(&store);
    nhv_vmcs12_vmptrld(&store, 0x1000ull);
    nhv_vmcs12_vmwrite(&store, NHV_HOST_RIP, 0xDEADull);
    nhv_vmcs12_vmwrite(&store, NHV_HOST_CR3, 0xBEEFull);

    r = nhv_vmcs02_build(nhv_vmcs12_current(&store), &caps, &reqs, &host, &plan);
    CHECK(r.kind == NHV_OK);
    CHECK(plan.host.rip == host.rip);
    CHECK(plan.host.rsp == host.rsp);
    CHECK(plan.host.cr0 == host.cr0);
    CHECK(plan.host.cr3 == host.cr3);
    CHECK(plan.host.cr4 == host.cr4);
    CHECK(plan.host.efer == host.efer);
}

static void test_end_to_end(void)
{
    NHV_VMCS_CAPS caps;
    NHV_L0_CONTROL_REQS reqs;
    NHV_L0_HOST_STATE host;
    NHV_VMCS12_STORE store;
    NHV_VMCS02_PLAN plan;
    NHV_RESULT r;

    caps_flexible(&caps);
    reqs_zero(&reqs);
    host_default(&host);

    reqs.proc_must1 = 0x00000010u; /* L0 always enables a proc-based control */

    nhv_vmcs12_store_init(&store);
    nhv_vmcs12_vmptrld(&store, 0x1000ull);
    nhv_vmcs12_vmwrite(&store, NHV_PIN_CONTROLS,     0x1u);
    nhv_vmcs12_vmwrite(&store, NHV_PRIMARY_CONTROLS, 0x20u);
    nhv_vmcs12_vmwrite(&store, NHV_GUEST_RIP,        0xAAAAull);

    r = nhv_vmcs02_build(nhv_vmcs12_current(&store), &caps, &reqs, &host, &plan);
    CHECK(r.kind == NHV_OK);
    CHECK(plan.pin_controls == 0x1u);
    CHECK(plan.primary_controls == 0x30u); /* 0x20 | 0x10 */
    CHECK(plan.guest_rip == 0xAAAAull);
}

static void test_null_args_fail(void)
{
    NHV_VMCS_CAPS caps;
    NHV_L0_CONTROL_REQS reqs;
    NHV_L0_HOST_STATE host;
    NHV_VMCS12_STORE store;
    NHV_VMCS02_PLAN plan;
    NHV_RESULT r;

    caps_flexible(&caps);
    reqs_zero(&reqs);
    host_default(&host);
    nhv_vmcs12_store_init(&store);
    nhv_vmcs12_vmptrld(&store, 0x1000ull);

    r = nhv_vmcs02_build(0, &caps, &reqs, &host, &plan);
    CHECK(r.kind == NHV_VMFAIL_VALID);
}

int main(void)
{
    test_basic_merge();
    test_l1_sets_must_be_zero_bit();
    test_l1_clears_must_be_one_bit();
    test_secondary_controls_merge();
    test_secondary_unsupported();
    test_l1_secondary_bit_invalid();
    test_guest_state_passthrough();
    test_host_state_from_l0();
    test_end_to_end();
    test_null_args_fail();

    printf("PASS %d  FAIL %d\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
