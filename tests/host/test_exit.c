#include "nhv_exit.h"

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

static void l1_zero(NHV_L1_CONTROLS* l1)
{
    l1->pin = 0;
    l1->proc = 0;
    l1->proc2 = 0;
    l1->exception_bitmap = 0;
}

static NHV_EXIT_INFO info(uint32_t reason)
{
    NHV_EXIT_INFO i;
    i.basic_reason = reason;
    i.exception_vector = 0;
    i.qualification = 0;
    i.io_bitmap_bit = 0;
    i.msr_bitmap_bit = 0;
    return i;
}

static void test_vmx_instructions_l0_owned(void)
{
    NHV_L1_CONTROLS l1;
    NHV_EXIT_INFO e;
    l1_zero(&l1);

    e = info(NHV_EXIT_VMCALL);
    CHECK(nhv_exit_classify(&e, &l1) == NHV_EXIT_L0_HANDLE);
    e = info(NHV_EXIT_VMREAD);
    CHECK(nhv_exit_classify(&e, &l1) == NHV_EXIT_L0_HANDLE);
    e = info(NHV_EXIT_VMWRITE);
    CHECK(nhv_exit_classify(&e, &l1) == NHV_EXIT_L0_HANDLE);
    e = info(NHV_EXIT_VMLAUNCH);
    CHECK(nhv_exit_classify(&e, &l1) == NHV_EXIT_L0_HANDLE);
    e = info(NHV_EXIT_VMXON);
    CHECK(nhv_exit_classify(&e, &l1) == NHV_EXIT_L0_HANDLE);
    e = info(NHV_EXIT_VMXOFF);
    CHECK(nhv_exit_classify(&e, &l1) == NHV_EXIT_L0_HANDLE);
    e = info(NHV_EXIT_VMPTRLD);
    CHECK(nhv_exit_classify(&e, &l1) == NHV_EXIT_L0_HANDLE);
    e = info(NHV_EXIT_VMCLEAR);
    CHECK(nhv_exit_classify(&e, &l1) == NHV_EXIT_L0_HANDLE);
}

static void test_unconditional_l0_owned(void)
{
    NHV_L1_CONTROLS l1;
    NHV_EXIT_INFO e;
    l1_zero(&l1);

    e = info(NHV_EXIT_CPUID);
    CHECK(nhv_exit_classify(&e, &l1) == NHV_EXIT_L0_HANDLE);
    e = info(NHV_EXIT_INVD);
    CHECK(nhv_exit_classify(&e, &l1) == NHV_EXIT_L0_HANDLE);
    e = info(NHV_EXIT_XSETBV);
    CHECK(nhv_exit_classify(&e, &l1) == NHV_EXIT_L0_HANDLE);
}

static void test_conditional_primary(void)
{
    NHV_L1_CONTROLS l1;
    NHV_EXIT_INFO e;
    l1_zero(&l1);

    /* HLT: reflect only when L1 set HLT exiting. */
    e = info(NHV_EXIT_HLT);
    CHECK(nhv_exit_classify(&e, &l1) == NHV_EXIT_L0_HANDLE);
    l1.proc = NHV_PROC_HLT_EXITING;
    CHECK(nhv_exit_classify(&e, &l1) == NHV_EXIT_REFLECT);

    /* RDTSC. */
    l1_zero(&l1);
    e = info(NHV_EXIT_RDTSC);
    CHECK(nhv_exit_classify(&e, &l1) == NHV_EXIT_L0_HANDLE);
    l1.proc = NHV_PROC_RDTSC_EXITING;
    CHECK(nhv_exit_classify(&e, &l1) == NHV_EXIT_REFLECT);

    /* MOV DR. */
    l1_zero(&l1);
    e = info(NHV_EXIT_MOV_DR);
    CHECK(nhv_exit_classify(&e, &l1) == NHV_EXIT_L0_HANDLE);
    l1.proc = NHV_PROC_MOV_DR_EXITING;
    CHECK(nhv_exit_classify(&e, &l1) == NHV_EXIT_REFLECT);

    /* PAUSE. */
    l1_zero(&l1);
    e = info(NHV_EXIT_PAUSE);
    CHECK(nhv_exit_classify(&e, &l1) == NHV_EXIT_L0_HANDLE);
    l1.proc = NHV_PROC_PAUSE_EXITING;
    CHECK(nhv_exit_classify(&e, &l1) == NHV_EXIT_REFLECT);

    /* MONITOR/MWAIT share the MONITOR exiting bit. */
    l1_zero(&l1);
    e = info(NHV_EXIT_MONITOR);
    CHECK(nhv_exit_classify(&e, &l1) == NHV_EXIT_L0_HANDLE);
    l1.proc = NHV_PROC_MONITOR_EXITING;
    CHECK(nhv_exit_classify(&e, &l1) == NHV_EXIT_REFLECT);
    e = info(NHV_EXIT_MWAIT);
    CHECK(nhv_exit_classify(&e, &l1) == NHV_EXIT_REFLECT);
}

static void test_io_and_msr(void)
{
    NHV_L1_CONTROLS l1;
    NHV_EXIT_INFO e;
    l1_zero(&l1);

    /* I/O: no control -> L0 handles. */
    e = info(NHV_EXIT_IO_INSTR);
    CHECK(nhv_exit_classify(&e, &l1) == NHV_EXIT_L0_HANDLE);

    /* Unconditional I/O exiting -> always reflect. */
    l1.proc = NHV_PROC_UNCOND_IO_EXITING;
    CHECK(nhv_exit_classify(&e, &l1) == NHV_EXIT_REFLECT);

    /* I/O bitmaps: reflect only when the bitmap bit is set. */
    l1_zero(&l1);
    l1.proc = NHV_PROC_USE_IO_BITMAPS;
    e.io_bitmap_bit = 0;
    CHECK(nhv_exit_classify(&e, &l1) == NHV_EXIT_L0_HANDLE);
    e.io_bitmap_bit = 1;
    CHECK(nhv_exit_classify(&e, &l1) == NHV_EXIT_REFLECT);

    /* MSR: without MSR bitmaps, RDMSR/WRMSR exit unconditionally -> L0. */
    l1_zero(&l1);
    e = info(NHV_EXIT_RDMSR);
    CHECK(nhv_exit_classify(&e, &l1) == NHV_EXIT_L0_HANDLE);
    e = info(NHV_EXIT_WRMSR);
    CHECK(nhv_exit_classify(&e, &l1) == NHV_EXIT_L0_HANDLE);

    /* With MSR bitmaps, reflect only when the bitmap bit is set. */
    l1.proc = NHV_PROC_USE_MSR_BITMAPS;
    e = info(NHV_EXIT_RDMSR);
    e.msr_bitmap_bit = 0;
    CHECK(nhv_exit_classify(&e, &l1) == NHV_EXIT_L0_HANDLE);
    e.msr_bitmap_bit = 1;
    CHECK(nhv_exit_classify(&e, &l1) == NHV_EXIT_REFLECT);
}

static void test_exception_and_nmi(void)
{
    NHV_L1_CONTROLS l1;
    NHV_EXIT_INFO e;
    l1_zero(&l1);

    /* Exception #GP (13) not in bitmap -> L0. In bitmap -> reflect. */
    e = info(NHV_EXIT_EXCEPTION_NMI);
    e.exception_vector = 13;
    CHECK(nhv_exit_classify(&e, &l1) == NHV_EXIT_L0_HANDLE);
    l1.exception_bitmap = 1u << 13;
    CHECK(nhv_exit_classify(&e, &l1) == NHV_EXIT_REFLECT);

    /* NMI (vector 2) uses the pin NMI-exiting bit, not the bitmap. */
    l1_zero(&l1);
    e.exception_vector = 2;
    CHECK(nhv_exit_classify(&e, &l1) == NHV_EXIT_L0_HANDLE);
    l1.pin = NHV_PIN_NMI_EXITING;
    CHECK(nhv_exit_classify(&e, &l1) == NHV_EXIT_REFLECT);

    /* External interrupt uses pin bit 0. */
    l1_zero(&l1);
    e = info(NHV_EXIT_EXTERNAL_INTERRUPT);
    CHECK(nhv_exit_classify(&e, &l1) == NHV_EXIT_L0_HANDLE);
    l1.pin = NHV_PIN_EXT_INTR_EXITING;
    CHECK(nhv_exit_classify(&e, &l1) == NHV_EXIT_REFLECT);
}

static void test_cr_access(void)
{
    NHV_L1_CONTROLS l1;
    NHV_EXIT_INFO e;
    l1_zero(&l1);

    /* CR3 load: qualification CR=3, store=0. */
    e = info(NHV_EXIT_CR_ACCESS);
    e.qualification = NHV_CR_NUMBER_CR3; /* load */
    CHECK(nhv_exit_classify(&e, &l1) == NHV_EXIT_L0_HANDLE);
    l1.proc = NHV_PROC_CR3_LOAD_EXITING;
    CHECK(nhv_exit_classify(&e, &l1) == NHV_EXIT_REFLECT);

    /* CR3 store: qualification CR=3, store=1. */
    l1_zero(&l1);
    e.qualification = NHV_CR_NUMBER_CR3 | NHV_CR_QUAL_ACCESS_STORE;
    CHECK(nhv_exit_classify(&e, &l1) == NHV_EXIT_L0_HANDLE);
    l1.proc = NHV_PROC_CR3_STORE_EXITING;
    CHECK(nhv_exit_classify(&e, &l1) == NHV_EXIT_REFLECT);

    /* CR8 load / store. */
    l1_zero(&l1);
    e.qualification = NHV_CR_NUMBER_CR8;
    CHECK(nhv_exit_classify(&e, &l1) == NHV_EXIT_L0_HANDLE);
    l1.proc = NHV_PROC_CR8_LOAD_EXITING;
    CHECK(nhv_exit_classify(&e, &l1) == NHV_EXIT_REFLECT);
    l1_zero(&l1);
    e.qualification = NHV_CR_NUMBER_CR8 | NHV_CR_QUAL_ACCESS_STORE;
    CHECK(nhv_exit_classify(&e, &l1) == NHV_EXIT_L0_HANDLE);
    l1.proc = NHV_PROC_CR8_STORE_EXITING;
    CHECK(nhv_exit_classify(&e, &l1) == NHV_EXIT_REFLECT);

    /* CR0 access is never reflected through these bits (mask-based). */
    l1_zero(&l1);
    l1.proc = NHV_PROC_CR3_LOAD_EXITING;
    e.qualification = NHV_CR_NUMBER_CR0;
    CHECK(nhv_exit_classify(&e, &l1) == NHV_EXIT_L0_HANDLE);
}

static void test_secondary_controls(void)
{
    NHV_L1_CONTROLS l1;
    NHV_EXIT_INFO e;
    l1_zero(&l1);

    e = info(NHV_EXIT_WBINVD);
    CHECK(nhv_exit_classify(&e, &l1) == NHV_EXIT_L0_HANDLE);
    l1.proc2 = NHV_PROC2_WBINVD_EXITING;
    CHECK(nhv_exit_classify(&e, &l1) == NHV_EXIT_REFLECT);

    l1_zero(&l1);
    e = info(NHV_EXIT_RDRAND);
    CHECK(nhv_exit_classify(&e, &l1) == NHV_EXIT_L0_HANDLE);
    l1.proc2 = NHV_PROC2_RDRAND_EXITING;
    CHECK(nhv_exit_classify(&e, &l1) == NHV_EXIT_REFLECT);

    l1_zero(&l1);
    e = info(NHV_EXIT_XSAVES);
    CHECK(nhv_exit_classify(&e, &l1) == NHV_EXIT_L0_HANDLE);
    l1.proc2 = NHV_PROC2_ENABLE_XSAVES;
    CHECK(nhv_exit_classify(&e, &l1) == NHV_EXIT_REFLECT);
    e = info(NHV_EXIT_XRSTORS);
    CHECK(nhv_exit_classify(&e, &l1) == NHV_EXIT_REFLECT);
}

static void test_unsupported(void)
{
    NHV_L1_CONTROLS l1;
    NHV_EXIT_INFO e;
    l1_zero(&l1);

    e = info(NHV_EXIT_TRIPLE_FAULT);
    CHECK(nhv_exit_classify(&e, &l1) == NHV_EXIT_UNSUPPORTED);
    e = info(NHV_EXIT_ENTRY_FAILURE);
    CHECK(nhv_exit_classify(&e, &l1) == NHV_EXIT_UNSUPPORTED);
    e = info(0xFFFFu); /* unknown reason */
    CHECK(nhv_exit_classify(&e, &l1) == NHV_EXIT_UNSUPPORTED);
}

static void test_reflect_writes_vmcs12(void)
{
    NHV_VMCS12_STORE store;
    NHV_REFLECT_INFO r;
    NHV_RESULT res;
    uint64_t v = 0;

    nhv_vmcs12_store_init(&store);
    res = nhv_vmcs12_vmptrld(&store, 0x1000ull);
    CHECK(res.kind == NHV_OK);

    r.exit_reason = NHV_EXIT_HLT;
    r.qualification = 0x1234u;
    r.guest_rip = 0x1111ull;
    r.guest_rsp = 0x2222ull;
    r.guest_rflags = 0x3333ull;

    res = nhv_exit_reflect(&store, &r);
    CHECK(res.kind == NHV_OK);

    res = nhv_vmcs12_vmread(&store, NHV_EXIT_REASON, &v);
    CHECK(res.kind == NHV_OK);
    CHECK(v == NHV_EXIT_HLT);

    res = nhv_vmcs12_vmread(&store, NHV_EXIT_QUALIFICATION, &v);
    CHECK(res.kind == NHV_OK);
    CHECK(v == 0x1234ull);

    res = nhv_vmcs12_vmread(&store, NHV_GUEST_RIP, &v);
    CHECK(res.kind == NHV_OK);
    CHECK(v == 0x1111ull);

    res = nhv_vmcs12_vmread(&store, NHV_GUEST_RSP, &v);
    CHECK(res.kind == NHV_OK);
    CHECK(v == 0x2222ull);

    res = nhv_vmcs12_vmread(&store, NHV_GUEST_RFLAGS, &v);
    CHECK(res.kind == NHV_OK);
    CHECK(v == 0x3333ull);
}

static void test_reflect_no_current_fails(void)
{
    NHV_VMCS12_STORE store;
    NHV_REFLECT_INFO r;
    NHV_RESULT res;

    nhv_vmcs12_store_init(&store);
    r.exit_reason = NHV_EXIT_HLT;
    r.qualification = 0;
    r.guest_rip = r.guest_rsp = r.guest_rflags = 0;

    res = nhv_exit_reflect(&store, &r);
    CHECK(res.kind == NHV_VMFAIL_INVALID);
}

static void test_l1_still_cannot_write_exit_reason(void)
{
    NHV_VMCS12_STORE store;
    NHV_RESULT res;

    nhv_vmcs12_store_init(&store);
    res = nhv_vmcs12_vmptrld(&store, 0x1000ull);
    CHECK(res.kind == NHV_OK);

    /* L1's own VMWRITE to the read-only exit-reason field still fails. */
    res = nhv_vmcs12_vmwrite(&store, NHV_EXIT_REASON, 0xAAu);
    CHECK(res.kind == NHV_VMFAIL_VALID);
    CHECK(res.vm_instr_error == NHV_VMERR_WRITE_TO_READONLY);
}

int main(void)
{
    test_vmx_instructions_l0_owned();
    test_unconditional_l0_owned();
    test_conditional_primary();
    test_io_and_msr();
    test_exception_and_nmi();
    test_cr_access();
    test_secondary_controls();
    test_unsupported();
    test_reflect_writes_vmcs12();
    test_reflect_no_current_fails();
    test_l1_still_cannot_write_exit_reason();

    printf("PASS %d  FAIL %d\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
