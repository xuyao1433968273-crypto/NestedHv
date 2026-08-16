#include "nhv_vmcs12.h"

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

static void test_encoding_scheme(void)
{
    CHECK(NHV_FIELD_WIDTH(NHV_GUEST_CS_SELECTOR) == NHV_WIDTH_16);
    CHECK(NHV_FIELD_TYPE(NHV_GUEST_CS_SELECTOR) == NHV_TYPE_GUEST);
    CHECK(NHV_FIELD_WIDTH(NHV_GUEST_RIP) == NHV_WIDTH_NAT);
    CHECK(NHV_FIELD_TYPE(NHV_GUEST_RIP) == NHV_TYPE_GUEST);
    CHECK(NHV_FIELD_WIDTH(NHV_EXIT_REASON) == NHV_WIDTH_32);
    CHECK(NHV_FIELD_TYPE(NHV_EXIT_REASON) == NHV_TYPE_RO);
    CHECK(NHV_FIELD_TYPE(NHV_HOST_RIP) == NHV_TYPE_HOST);
    /* Distinct fields must have distinct encodings. */
    CHECK(NHV_GUEST_RIP != NHV_HOST_RIP);
    CHECK(NHV_GUEST_RSP != NHV_GUEST_RIP);
}

static void test_field_lookup(void)
{
    const NHV_FIELD_DESC* desc;

    desc = nhv_field_lookup(NHV_GUEST_RIP);
    CHECK(desc != 0);
    if (desc != 0) {
        CHECK(desc->width == NHV_WIDTH_NAT);
        CHECK(desc->access == NHV_ACCESS_RW);
    }

    desc = nhv_field_lookup(NHV_EXIT_REASON);
    CHECK(desc != 0);
    if (desc != 0) {
        CHECK(desc->access == NHV_ACCESS_RO);
    }

    CHECK(nhv_field_lookup(0xFFFFFFFFu) == 0);
    /* 16-bit control index 6 is reserved (VPID=0, posted-intr vec=2, EPTP index=4). */
    CHECK(nhv_field_lookup(NHV_FIELD_ENCODE(NHV_WIDTH_16, 0, NHV_TYPE_CONTROL, 0x06)) == 0);
    /* High-half encoding of a 64-bit field is not modeled yet. */
    CHECK(nhv_field_lookup(NHV_FIELD_ENCODE(NHV_WIDTH_32, 1, NHV_TYPE_GUEST, 0x06)) == 0);
}

static void test_field_count(void)
{
    CHECK(nhv_field_count() > 80u);
}

static void test_round_trip(void)
{
    NHV_VMCS12_STORE store;
    NHV_RESULT r;
    uint64_t v = 0;

    nhv_vmcs12_store_init(&store);
    r = nhv_vmcs12_vmptrld(&store, 0x1000ull);
    CHECK(r.kind == NHV_OK);

    r = nhv_vmcs12_vmwrite(&store, NHV_GUEST_RIP, 0x1122334455667788ull);
    CHECK(r.kind == NHV_OK);

    r = nhv_vmcs12_vmread(&store, NHV_GUEST_RIP, &v);
    CHECK(r.kind == NHV_OK);
    CHECK(v == 0x1122334455667788ull);

    /* Overwrite must replace, not append. */
    r = nhv_vmcs12_vmwrite(&store, NHV_GUEST_RIP, 1ull);
    CHECK(r.kind == NHV_OK);
    r = nhv_vmcs12_vmread(&store, NHV_GUEST_RIP, &v);
    CHECK(v == 1ull);
}

static void test_read_before_write_returns_zero(void)
{
    NHV_VMCS12_STORE store;
    NHV_RESULT r;
    uint64_t v = 0xdeadbeef;

    nhv_vmcs12_store_init(&store);
    r = nhv_vmcs12_vmptrld(&store, 0x1000ull);
    CHECK(r.kind == NHV_OK);
    r = nhv_vmcs12_vmread(&store, NHV_GUEST_CR3, &v);
    CHECK(r.kind == NHV_OK);
    CHECK(v == 0);
}

static void test_readonly_write_fails(void)
{
    NHV_VMCS12_STORE store;
    NHV_RESULT r;

    nhv_vmcs12_store_init(&store);
    r = nhv_vmcs12_vmptrld(&store, 0x1000ull);
    CHECK(r.kind == NHV_OK);
    r = nhv_vmcs12_vmwrite(&store, NHV_EXIT_REASON, 10u);
    CHECK(r.kind == NHV_VMFAIL_VALID);
    CHECK(r.vm_instr_error == NHV_VMERR_WRITE_TO_READONLY);

    /* VMREAD of a read-only field is still allowed. */
    {
        uint64_t v = 0;
        r = nhv_vmcs12_vmread(&store, NHV_EXIT_REASON, &v);
        CHECK(r.kind == NHV_OK);
        CHECK(v == 0);
    }
}

static void test_unknown_encoding_fails_invalid(void)
{
    NHV_VMCS12_STORE store;
    NHV_RESULT r;
    uint64_t v = 0;

    nhv_vmcs12_store_init(&store);
    r = nhv_vmcs12_vmread(&store, 0xFFFFFFFFu, &v);
    CHECK(r.kind == NHV_VMFAIL_INVALID);
    r = nhv_vmcs12_vmwrite(&store, 0xFFFFFFFFu, 1ull);
    CHECK(r.kind == NHV_VMFAIL_INVALID);
}

static void test_no_current_object(void)
{
    NHV_VMCS12_STORE store;
    NHV_RESULT r;
    uint64_t v = 0;

    nhv_vmcs12_store_init(&store);
    /* No VMPTRLD yet: current is absent. */
    r = nhv_vmcs12_vmread(&store, NHV_GUEST_RIP, &v);
    CHECK(r.kind == NHV_VMFAIL_INVALID);
    r = nhv_vmcs12_vmwrite(&store, NHV_GUEST_RIP, 1ull);
    CHECK(r.kind == NHV_VMFAIL_INVALID);
    r = nhv_vmcs12_vmptrst(&store, &v);
    CHECK(r.kind == NHV_VMFAIL_INVALID);
}

static void test_vmptrld_vmptrst(void)
{
    NHV_VMCS12_STORE store;
    NHV_RESULT r;
    uint64_t gpa = 0;

    nhv_vmcs12_store_init(&store);

    r = nhv_vmcs12_vmptrld(&store, 0x1000ull);
    CHECK(r.kind == NHV_OK);
    r = nhv_vmcs12_vmptrst(&store, &gpa);
    CHECK(r.kind == NHV_OK);
    CHECK(gpa == 0x1000ull);

    /* Switching to a different object must not carry values over. */
    r = nhv_vmcs12_vmwrite(&store, NHV_GUEST_RIP, 0xabcull);
    CHECK(r.kind == NHV_OK);
    r = nhv_vmcs12_vmptrld(&store, 0x2000ull);
    CHECK(r.kind == NHV_OK);
    {
        uint64_t v = 0xffff;
        r = nhv_vmcs12_vmread(&store, NHV_GUEST_RIP, &v);
        CHECK(r.kind == NHV_OK);
        CHECK(v == 0);
    }

    /* Switching back restores the first object's value. */
    r = nhv_vmcs12_vmptrld(&store, 0x1000ull);
    CHECK(r.kind == NHV_OK);
    {
        uint64_t v = 0;
        r = nhv_vmcs12_vmread(&store, NHV_GUEST_RIP, &v);
        CHECK(r.kind == NHV_OK);
        CHECK(v == 0xabcull);
    }
}

static void test_launch_state_machine(void)
{
    NHV_VMCS12_STORE store;
    NHV_RESULT r;

    nhv_vmcs12_store_init(&store);
    r = nhv_vmcs12_vmptrld(&store, 0x1000ull);
    CHECK(r.kind == NHV_OK);

    /* VMRESUME before VMLAUNCH fails. */
    r = nhv_vmcs12_begin_resume(&store);
    CHECK(r.kind == NHV_VMFAIL_VALID);
    CHECK(r.vm_instr_error == 4u);

    /* VMLAUNCH on clear succeeds and marks launched. */
    r = nhv_vmcs12_begin_launch(&store);
    CHECK(r.kind == NHV_OK);
    CHECK(nhv_vmcs12_launch_state(&store) == NHV_VMCS12_LAUNCH_LAUNCHED);

    /* VMLAUNCH again fails. */
    r = nhv_vmcs12_begin_launch(&store);
    CHECK(r.kind == NHV_VMFAIL_VALID);
    CHECK(r.vm_instr_error == 5u);

    /* VMRESUME on launched succeeds. */
    r = nhv_vmcs12_begin_resume(&store);
    CHECK(r.kind == NHV_OK);

    /* VMCLEAR resets to clear. */
    r = nhv_vmcs12_vmclear(&store, 0x1000ull);
    CHECK(r.kind == NHV_OK);
    CHECK(nhv_vmcs12_launch_state(&store) == NHV_VMCS12_LAUNCH_CLEAR);
}

int main(void)
{
    test_encoding_scheme();
    test_field_lookup();
    test_field_count();
    test_round_trip();
    test_read_before_write_returns_zero();
    test_readonly_write_fails();
    test_unknown_encoding_fails_invalid();
    test_no_current_object();
    test_vmptrld_vmptrst();
    test_launch_state_machine();

    printf("PASS %d  FAIL %d\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
