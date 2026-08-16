#include "nhv_exit.h"

static int is_vmx_instruction(uint32_t reason)
{
    switch (reason) {
    case NHV_EXIT_VMCALL:
    case NHV_EXIT_VMCLEAR:
    case NHV_EXIT_VMLAUNCH:
    case NHV_EXIT_VMPTRLD:
    case NHV_EXIT_VMPTRST:
    case NHV_EXIT_VMREAD:
    case NHV_EXIT_VMWRITE:
    case NHV_EXIT_VMXOFF:
    case NHV_EXIT_VMXON:
        return 1;
    default:
        return 0;
    }
}

NHV_EXIT_DISPOSITION nhv_exit_classify(const NHV_EXIT_INFO* info,
                                       const NHV_L1_CONTROLS* l1)
{
    uint32_t reason;

    if (info == 0 || l1 == 0) {
        return NHV_EXIT_UNSUPPORTED;
    }
    reason = info->basic_reason;

    /* VMX instructions always exit to L0; L0 emulates them against VMCS12. */
    if (is_vmx_instruction(reason)) {
        return NHV_EXIT_L0_HANDLE;
    }

    switch (reason) {
    /* Unconditional exits with no L1 control bit: L0 emulates. */
    case NHV_EXIT_CPUID:
    case NHV_EXIT_INVD:
    case NHV_EXIT_XSETBV:
        return NHV_EXIT_L0_HANDLE;

    /* Exception or NMI: NMI uses the pin control; exceptions use the bitmap. */
    case NHV_EXIT_EXCEPTION_NMI:
        if (info->exception_vector == 2u) {
            return (l1->pin & NHV_PIN_NMI_EXITING) != 0
                       ? NHV_EXIT_REFLECT
                       : NHV_EXIT_L0_HANDLE;
        }
        if (info->exception_vector < 32u &&
            (l1->exception_bitmap & (1u << info->exception_vector)) != 0) {
            return NHV_EXIT_REFLECT;
        }
        return NHV_EXIT_L0_HANDLE;

    case NHV_EXIT_EXTERNAL_INTERRUPT:
        return (l1->pin & NHV_PIN_EXT_INTR_EXITING) != 0
                   ? NHV_EXIT_REFLECT
                   : NHV_EXIT_L0_HANDLE;

    case NHV_EXIT_HLT:
        return (l1->proc & NHV_PROC_HLT_EXITING) != 0
                   ? NHV_EXIT_REFLECT
                   : NHV_EXIT_L0_HANDLE;

    case NHV_EXIT_RDTSC:
        return (l1->proc & NHV_PROC_RDTSC_EXITING) != 0
                   ? NHV_EXIT_REFLECT
                   : NHV_EXIT_L0_HANDLE;

    case NHV_EXIT_MOV_DR:
        return (l1->proc & NHV_PROC_MOV_DR_EXITING) != 0
                   ? NHV_EXIT_REFLECT
                   : NHV_EXIT_L0_HANDLE;

    case NHV_EXIT_CR_ACCESS: {
        uint32_t cr = info->qualification & NHV_CR_QUAL_NUMBER_MASK;
        int store = (info->qualification & NHV_CR_QUAL_ACCESS_STORE) != 0;
        switch (cr) {
        case NHV_CR_NUMBER_CR3:
            if (!store) {
                return (l1->proc & NHV_PROC_CR3_LOAD_EXITING) != 0
                           ? NHV_EXIT_REFLECT
                           : NHV_EXIT_L0_HANDLE;
            }
            return (l1->proc & NHV_PROC_CR3_STORE_EXITING) != 0
                       ? NHV_EXIT_REFLECT
                       : NHV_EXIT_L0_HANDLE;
        case NHV_CR_NUMBER_CR8:
            if (!store) {
                return (l1->proc & NHV_PROC_CR8_LOAD_EXITING) != 0
                           ? NHV_EXIT_REFLECT
                           : NHV_EXIT_L0_HANDLE;
            }
            return (l1->proc & NHV_PROC_CR8_STORE_EXITING) != 0
                       ? NHV_EXIT_REFLECT
                       : NHV_EXIT_L0_HANDLE;
        default:
            /* CR0/CR4 are handled through the guest/host masks, not these bits. */
            return NHV_EXIT_L0_HANDLE;
        }
    }

    case NHV_EXIT_IO_INSTR:
        if ((l1->proc & NHV_PROC_UNCOND_IO_EXITING) != 0) {
            return NHV_EXIT_REFLECT;
        }
        if ((l1->proc & NHV_PROC_USE_IO_BITMAPS) != 0) {
            return info->io_bitmap_bit != 0 ? NHV_EXIT_REFLECT : NHV_EXIT_L0_HANDLE;
        }
        return NHV_EXIT_L0_HANDLE;

    case NHV_EXIT_RDMSR:
    case NHV_EXIT_WRMSR:
        if ((l1->proc & NHV_PROC_USE_MSR_BITMAPS) != 0) {
            return info->msr_bitmap_bit != 0 ? NHV_EXIT_REFLECT : NHV_EXIT_L0_HANDLE;
        }
        /* RDMSR/WRMSR exit unconditionally; L1 did not ask, so L0 emulates. */
        return NHV_EXIT_L0_HANDLE;

    case NHV_EXIT_MWAIT:
    case NHV_EXIT_MONITOR:
        return (l1->proc & NHV_PROC_MONITOR_EXITING) != 0
                   ? NHV_EXIT_REFLECT
                   : NHV_EXIT_L0_HANDLE;

    case NHV_EXIT_PAUSE:
        return (l1->proc & NHV_PROC_PAUSE_EXITING) != 0
                   ? NHV_EXIT_REFLECT
                   : NHV_EXIT_L0_HANDLE;

    case NHV_EXIT_WBINVD:
        return (l1->proc2 & NHV_PROC2_WBINVD_EXITING) != 0
                   ? NHV_EXIT_REFLECT
                   : NHV_EXIT_L0_HANDLE;

    case NHV_EXIT_RDRAND:
        return (l1->proc2 & NHV_PROC2_RDRAND_EXITING) != 0
                   ? NHV_EXIT_REFLECT
                   : NHV_EXIT_L0_HANDLE;

    case NHV_EXIT_INVPCID:
        return (l1->proc2 & NHV_PROC2_ENABLE_INVPCID) != 0
                   ? NHV_EXIT_REFLECT
                   : NHV_EXIT_L0_HANDLE;

    case NHV_EXIT_RDSEED:
        return (l1->proc2 & NHV_PROC2_RDSEED_EXITING) != 0
                   ? NHV_EXIT_REFLECT
                   : NHV_EXIT_L0_HANDLE;

    case NHV_EXIT_XSAVES:
    case NHV_EXIT_XRSTORS:
        return (l1->proc2 & NHV_PROC2_ENABLE_XSAVES) != 0
                   ? NHV_EXIT_REFLECT
                   : NHV_EXIT_L0_HANDLE;

    case NHV_EXIT_UMWAIT:
    case NHV_EXIT_TPAUSE:
        return (l1->proc2 & NHV_PROC2_ENABLE_UMWAIT_TPAUSE) != 0
                   ? NHV_EXIT_REFLECT
                   : NHV_EXIT_L0_HANDLE;

    /* Neither side owns these: fail closed. */
    case NHV_EXIT_TRIPLE_FAULT:
    case NHV_EXIT_ENTRY_FAILURE:
    default:
        return NHV_EXIT_UNSUPPORTED;
    }
}

NHV_RESULT nhv_exit_reflect(NHV_VMCS12_STORE* store, const NHV_REFLECT_INFO* info)
{
    NHV_RESULT r;

    if (store == 0 || info == 0) {
        NHV_RESULT bad;
        bad.kind = NHV_VMFAIL_INVALID;
        bad.vm_instr_error = 0;
        return bad;
    }

    r = nhv_vmcs12_write_raw(store, NHV_EXIT_REASON, info->exit_reason);
    if (r.kind != NHV_OK) {
        return r;
    }
    r = nhv_vmcs12_write_raw(store, NHV_EXIT_QUALIFICATION, info->qualification);
    if (r.kind != NHV_OK) {
        return r;
    }
    r = nhv_vmcs12_vmwrite(store, NHV_GUEST_RIP, info->guest_rip);
    if (r.kind != NHV_OK) {
        return r;
    }
    r = nhv_vmcs12_vmwrite(store, NHV_GUEST_RSP, info->guest_rsp);
    if (r.kind != NHV_OK) {
        return r;
    }
    r = nhv_vmcs12_vmwrite(store, NHV_GUEST_RFLAGS, info->guest_rflags);
    return r;
}
