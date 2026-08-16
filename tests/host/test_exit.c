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

static NHV_EXIT_SNAPSHOT filled_snapshot(void)
{
    NHV_EXIT_SNAPSHOT s;
    s.exit_reason = NHV_EXIT_HLT;
    s.exit_qualification = 0x1001u;
    s.exit_interruption_info = 0x1002u;
    s.exit_interruption_error = 0x1003u;
    s.exit_instruction_length = 0x1004u;
    s.idt_vectoring_info = 0x1005u;
    s.idt_vectoring_error = 0x1006u;
    s.vm_instruction_error = 0x1007u;
    s.vmx_instruction_info = 0x1008u;
    s.guest_linear_address = 0x1009ull;
    s.guest_physical_address = 0x100Aull;
    s.guest_rip = 0x1011ull;
    s.guest_rsp = 0x1012ull;
    s.guest_rflags = 0x1013ull;
    s.guest_cr0 = 0x1014ull;
    s.guest_cr3 = 0x1015ull;
    s.guest_cr4 = 0x1016ull;
    s.guest_dr7 = 0x1017ull;
    s.guest_efer = 0x1018ull;
    s.guest_es_sel = 0x21u;
    s.guest_cs_sel = 0x22u;
    s.guest_ss_sel = 0x23u;
    s.guest_ds_sel = 0x24u;
    s.guest_fs_sel = 0x25u;
    s.guest_gs_sel = 0x26u;
    s.guest_ldtr_sel = 0x27u;
    s.guest_tr_sel = 0x28u;
    s.guest_es_limit = 0x31u;
    s.guest_cs_limit = 0x32u;
    s.guest_ss_limit = 0x33u;
    s.guest_ds_limit = 0x34u;
    s.guest_fs_limit = 0x35u;
    s.guest_gs_limit = 0x36u;
    s.guest_ldtr_limit = 0x37u;
    s.guest_tr_limit = 0x38u;
    s.guest_gdtr_limit = 0x39u;
    s.guest_idtr_limit = 0x3Au;
    s.guest_es_ar = 0x41u;
    s.guest_cs_ar = 0x42u;
    s.guest_ss_ar = 0x43u;
    s.guest_ds_ar = 0x44u;
    s.guest_fs_ar = 0x45u;
    s.guest_gs_ar = 0x46u;
    s.guest_ldtr_ar = 0x47u;
    s.guest_tr_ar = 0x48u;
    s.guest_es_base = 0x51ull;
    s.guest_cs_base = 0x52ull;
    s.guest_ss_base = 0x53ull;
    s.guest_ds_base = 0x54ull;
    s.guest_fs_base = 0x55ull;
    s.guest_gs_base = 0x56ull;
    s.guest_ldtr_base = 0x57ull;
    s.guest_tr_base = 0x58ull;
    s.guest_gdtr_base = 0x59ull;
    s.guest_idtr_base = 0x5Aull;
    s.guest_interruptibility = 0x61u;
    s.guest_activity_state = 0x62u;
    s.guest_sysenter_cs = 0x63u;
    s.guest_sysenter_esp = 0x64ull;
    s.guest_sysenter_eip = 0x65ull;
    s.guest_pending_dbg_exc = 0x66ull;
    return s;
}

static void test_reflect_writes_full_snapshot(void)
{
    NHV_VMCS12_STORE store;
    NHV_EXIT_SNAPSHOT s = filled_snapshot();
    NHV_RESULT res;
    uint64_t v = 0;

    nhv_vmcs12_store_init(&store);
    res = nhv_vmcs12_vmptrld(&store, 0x1000ull);
    CHECK(res.kind == NHV_OK);

    res = nhv_exit_reflect(&store, &s);
    CHECK(res.kind == NHV_OK);

    /* Exit-information fields. */
    res = nhv_vmcs12_vmread(&store, NHV_EXIT_REASON, &v);
    CHECK(res.kind == NHV_OK && v == s.exit_reason);
    res = nhv_vmcs12_vmread(&store, NHV_EXIT_QUALIFICATION, &v);
    CHECK(res.kind == NHV_OK && v == s.exit_qualification);
    res = nhv_vmcs12_vmread(&store, NHV_EXIT_INTERRUPTION_INFO, &v);
    CHECK(res.kind == NHV_OK && v == s.exit_interruption_info);
    res = nhv_vmcs12_vmread(&store, NHV_EXIT_INTERRUPTION_ERROR, &v);
    CHECK(res.kind == NHV_OK && v == s.exit_interruption_error);
    res = nhv_vmcs12_vmread(&store, NHV_EXIT_INSTRUCTION_LENGTH, &v);
    CHECK(res.kind == NHV_OK && v == s.exit_instruction_length);
    res = nhv_vmcs12_vmread(&store, NHV_IDT_VECTORING_INFO, &v);
    CHECK(res.kind == NHV_OK && v == s.idt_vectoring_info);
    res = nhv_vmcs12_vmread(&store, NHV_IDT_VECTORING_ERROR, &v);
    CHECK(res.kind == NHV_OK && v == s.idt_vectoring_error);
    res = nhv_vmcs12_vmread(&store, NHV_VM_INSTRUCTION_ERROR, &v);
    CHECK(res.kind == NHV_OK && v == s.vm_instruction_error);
    res = nhv_vmcs12_vmread(&store, NHV_EXIT_INSTRUCTION_INFO, &v);
    CHECK(res.kind == NHV_OK && v == s.vmx_instruction_info);
    res = nhv_vmcs12_vmread(&store, NHV_GUEST_LINEAR_ADDRESS, &v);
    CHECK(res.kind == NHV_OK && v == s.guest_linear_address);
    res = nhv_vmcs12_vmread(&store, NHV_GUEST_PHYSICAL_ADDRESS, &v);
    CHECK(res.kind == NHV_OK && v == s.guest_physical_address);

    /* Guest state. */
    res = nhv_vmcs12_vmread(&store, NHV_GUEST_RIP, &v);
    CHECK(res.kind == NHV_OK && v == s.guest_rip);
    res = nhv_vmcs12_vmread(&store, NHV_GUEST_RSP, &v);
    CHECK(res.kind == NHV_OK && v == s.guest_rsp);
    res = nhv_vmcs12_vmread(&store, NHV_GUEST_RFLAGS, &v);
    CHECK(res.kind == NHV_OK && v == s.guest_rflags);
    res = nhv_vmcs12_vmread(&store, NHV_GUEST_CR0, &v);
    CHECK(res.kind == NHV_OK && v == s.guest_cr0);
    res = nhv_vmcs12_vmread(&store, NHV_GUEST_CR3, &v);
    CHECK(res.kind == NHV_OK && v == s.guest_cr3);
    res = nhv_vmcs12_vmread(&store, NHV_GUEST_CR4, &v);
    CHECK(res.kind == NHV_OK && v == s.guest_cr4);
    res = nhv_vmcs12_vmread(&store, NHV_GUEST_DR7, &v);
    CHECK(res.kind == NHV_OK && v == s.guest_dr7);
    res = nhv_vmcs12_vmread(&store, NHV_GUEST_IA32_EFER, &v);
    CHECK(res.kind == NHV_OK && v == s.guest_efer);

    res = nhv_vmcs12_vmread(&store, NHV_GUEST_CS_SELECTOR, &v);
    CHECK(res.kind == NHV_OK && v == s.guest_cs_sel);
    res = nhv_vmcs12_vmread(&store, NHV_GUEST_SS_SELECTOR, &v);
    CHECK(res.kind == NHV_OK && v == s.guest_ss_sel);
    res = nhv_vmcs12_vmread(&store, NHV_GUEST_TR_SELECTOR, &v);
    CHECK(res.kind == NHV_OK && v == s.guest_tr_sel);

    res = nhv_vmcs12_vmread(&store, NHV_GUEST_CS_LIMIT, &v);
    CHECK(res.kind == NHV_OK && v == s.guest_cs_limit);
    res = nhv_vmcs12_vmread(&store, NHV_GUEST_GDTR_LIMIT, &v);
    CHECK(res.kind == NHV_OK && v == s.guest_gdtr_limit);
    res = nhv_vmcs12_vmread(&store, NHV_GUEST_CS_ACCESS, &v);
    CHECK(res.kind == NHV_OK && v == s.guest_cs_ar);
    res = nhv_vmcs12_vmread(&store, NHV_GUEST_TR_ACCESS, &v);
    CHECK(res.kind == NHV_OK && v == s.guest_tr_ar);

    res = nhv_vmcs12_vmread(&store, NHV_GUEST_CS_BASE, &v);
    CHECK(res.kind == NHV_OK && v == s.guest_cs_base);
    res = nhv_vmcs12_vmread(&store, NHV_GUEST_IDTR_BASE, &v);
    CHECK(res.kind == NHV_OK && v == s.guest_idtr_base);

    res = nhv_vmcs12_vmread(&store, NHV_GUEST_INTERRUPTIBILITY, &v);
    CHECK(res.kind == NHV_OK && v == s.guest_interruptibility);
    res = nhv_vmcs12_vmread(&store, NHV_GUEST_ACTIVITY_STATE, &v);
    CHECK(res.kind == NHV_OK && v == s.guest_activity_state);
    res = nhv_vmcs12_vmread(&store, NHV_GUEST_SYSENTER_EIP, &v);
    CHECK(res.kind == NHV_OK && v == s.guest_sysenter_eip);
    res = nhv_vmcs12_vmread(&store, NHV_GUEST_PENDING_DBG_EXC, &v);
    CHECK(res.kind == NHV_OK && v == s.guest_pending_dbg_exc);
}

static void test_reflect_no_current_fails(void)
{
    NHV_VMCS12_STORE store;
    NHV_EXIT_SNAPSHOT s = filled_snapshot();
    NHV_RESULT res;

    nhv_vmcs12_store_init(&store);
    res = nhv_exit_reflect(&store, &s);
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
    test_reflect_writes_full_snapshot();
    test_reflect_no_current_fails();
    test_l1_still_cannot_write_exit_reason();

    printf("PASS %d  FAIL %d\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
