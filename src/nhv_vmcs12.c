#include "nhv_vmcs12.h"

#include <string.h>

/*
 * Supported field table. Each entry maps an encoding to its width and access
 * classification. The table is generated from one list so the encoding and the
 * descriptor can never drift apart.
 *
 * First milestone simplification: only full-width encodings are modeled. The
 * low/high 32-bit half encodings of 64-bit fields are not in the table, so an
 * access using one of them fails VMfailInvalid. That matches the common L1
 * path (which uses full-width encodings) and is easy to extend later.
 */
#define NHV_FIELD_LIST(X)                                                    \
    X(NHV_VPID,                     NHV_WIDTH_16,  NHV_ACCESS_RW)            \
    X(NHV_GUEST_ES_SELECTOR,        NHV_WIDTH_16,  NHV_ACCESS_RW)            \
    X(NHV_GUEST_CS_SELECTOR,        NHV_WIDTH_16,  NHV_ACCESS_RW)            \
    X(NHV_GUEST_SS_SELECTOR,        NHV_WIDTH_16,  NHV_ACCESS_RW)            \
    X(NHV_GUEST_DS_SELECTOR,        NHV_WIDTH_16,  NHV_ACCESS_RW)            \
    X(NHV_GUEST_FS_SELECTOR,        NHV_WIDTH_16,  NHV_ACCESS_RW)            \
    X(NHV_GUEST_GS_SELECTOR,        NHV_WIDTH_16,  NHV_ACCESS_RW)            \
    X(NHV_GUEST_LDTR_SELECTOR,      NHV_WIDTH_16,  NHV_ACCESS_RW)            \
    X(NHV_GUEST_TR_SELECTOR,        NHV_WIDTH_16,  NHV_ACCESS_RW)            \
    X(NHV_HOST_ES_SELECTOR,         NHV_WIDTH_16,  NHV_ACCESS_RW)            \
    X(NHV_HOST_CS_SELECTOR,         NHV_WIDTH_16,  NHV_ACCESS_RW)            \
    X(NHV_HOST_SS_SELECTOR,         NHV_WIDTH_16,  NHV_ACCESS_RW)            \
    X(NHV_HOST_DS_SELECTOR,         NHV_WIDTH_16,  NHV_ACCESS_RW)            \
    X(NHV_HOST_FS_SELECTOR,         NHV_WIDTH_16,  NHV_ACCESS_RW)            \
    X(NHV_HOST_GS_SELECTOR,         NHV_WIDTH_16,  NHV_ACCESS_RW)            \
    X(NHV_HOST_TR_SELECTOR,         NHV_WIDTH_16,  NHV_ACCESS_RW)            \
    X(NHV_IO_BITMAP_A,              NHV_WIDTH_64,  NHV_ACCESS_RW)            \
    X(NHV_IO_BITMAP_B,              NHV_WIDTH_64,  NHV_ACCESS_RW)            \
    X(NHV_MSR_BITMAP_ADDRESS,       NHV_WIDTH_64,  NHV_ACCESS_RW)            \
    X(NHV_VMEXIT_MSR_STORE_ADDRESS, NHV_WIDTH_64,  NHV_ACCESS_RW)            \
    X(NHV_VMEXIT_MSR_LOAD_ADDRESS,  NHV_WIDTH_64,  NHV_ACCESS_RW)            \
    X(NHV_VMENTRY_MSR_LOAD_ADDRESS, NHV_WIDTH_64,  NHV_ACCESS_RW)            \
    X(NHV_TSC_OFFSET,               NHV_WIDTH_64,  NHV_ACCESS_RW)            \
    X(NHV_EPT_POINTER,              NHV_WIDTH_64,  NHV_ACCESS_RW)            \
    X(NHV_GUEST_VMCS_LINK_POINTER,  NHV_WIDTH_64,  NHV_ACCESS_RW)            \
    X(NHV_GUEST_IA32_DEBUGCTL,      NHV_WIDTH_64,  NHV_ACCESS_RW)            \
    X(NHV_GUEST_IA32_EFER,          NHV_WIDTH_64,  NHV_ACCESS_RW)            \
    X(NHV_HOST_IA32_EFER,           NHV_WIDTH_64,  NHV_ACCESS_RW)            \
    X(NHV_GUEST_PHYSICAL_ADDRESS,   NHV_WIDTH_64,  NHV_ACCESS_RO)            \
    X(NHV_PIN_CONTROLS,             NHV_WIDTH_32,  NHV_ACCESS_RW)            \
    X(NHV_PRIMARY_CONTROLS,         NHV_WIDTH_32,  NHV_ACCESS_RW)            \
    X(NHV_EXCEPTION_BITMAP,         NHV_WIDTH_32,  NHV_ACCESS_RW)            \
    X(NHV_PF_ERROR_CODE_MASK,       NHV_WIDTH_32,  NHV_ACCESS_RW)            \
    X(NHV_PF_ERROR_CODE_MATCH,      NHV_WIDTH_32,  NHV_ACCESS_RW)            \
    X(NHV_CR3_TARGET_COUNT,         NHV_WIDTH_32,  NHV_ACCESS_RW)            \
    X(NHV_EXIT_CONTROLS,            NHV_WIDTH_32,  NHV_ACCESS_RW)            \
    X(NHV_ENTRY_CONTROLS,           NHV_WIDTH_32,  NHV_ACCESS_RW)            \
    X(NHV_ENTRY_INTR_INFO,          NHV_WIDTH_32,  NHV_ACCESS_RW)            \
    X(NHV_ENTRY_EXCEPTION_ERROR,    NHV_WIDTH_32,  NHV_ACCESS_RW)            \
    X(NHV_ENTRY_INSTRUCTION_LENGTH, NHV_WIDTH_32,  NHV_ACCESS_RW)            \
    X(NHV_SECONDARY_CONTROLS,       NHV_WIDTH_32,  NHV_ACCESS_RW)            \
    X(NHV_VM_INSTRUCTION_ERROR,     NHV_WIDTH_32,  NHV_ACCESS_RO)            \
    X(NHV_EXIT_REASON,              NHV_WIDTH_32,  NHV_ACCESS_RO)            \
    X(NHV_EXIT_INTERRUPTION_INFO,   NHV_WIDTH_32,  NHV_ACCESS_RO)            \
    X(NHV_EXIT_INTERRUPTION_ERROR,  NHV_WIDTH_32,  NHV_ACCESS_RO)            \
    X(NHV_IDT_VECTORING_INFO,       NHV_WIDTH_32,  NHV_ACCESS_RO)            \
    X(NHV_IDT_VECTORING_ERROR,      NHV_WIDTH_32,  NHV_ACCESS_RO)            \
    X(NHV_EXIT_INSTRUCTION_LENGTH,  NHV_WIDTH_32,  NHV_ACCESS_RO)            \
    X(NHV_EXIT_INSTRUCTION_INFO,    NHV_WIDTH_32,  NHV_ACCESS_RO)            \
    X(NHV_GUEST_ES_LIMIT,           NHV_WIDTH_32,  NHV_ACCESS_RW)            \
    X(NHV_GUEST_CS_LIMIT,           NHV_WIDTH_32,  NHV_ACCESS_RW)            \
    X(NHV_GUEST_SS_LIMIT,           NHV_WIDTH_32,  NHV_ACCESS_RW)            \
    X(NHV_GUEST_DS_LIMIT,           NHV_WIDTH_32,  NHV_ACCESS_RW)            \
    X(NHV_GUEST_FS_LIMIT,           NHV_WIDTH_32,  NHV_ACCESS_RW)            \
    X(NHV_GUEST_GS_LIMIT,           NHV_WIDTH_32,  NHV_ACCESS_RW)            \
    X(NHV_GUEST_LDTR_LIMIT,         NHV_WIDTH_32,  NHV_ACCESS_RW)            \
    X(NHV_GUEST_TR_LIMIT,           NHV_WIDTH_32,  NHV_ACCESS_RW)            \
    X(NHV_GUEST_GDTR_LIMIT,         NHV_WIDTH_32,  NHV_ACCESS_RW)            \
    X(NHV_GUEST_IDTR_LIMIT,         NHV_WIDTH_32,  NHV_ACCESS_RW)            \
    X(NHV_GUEST_ES_ACCESS,          NHV_WIDTH_32,  NHV_ACCESS_RW)            \
    X(NHV_GUEST_CS_ACCESS,          NHV_WIDTH_32,  NHV_ACCESS_RW)            \
    X(NHV_GUEST_SS_ACCESS,          NHV_WIDTH_32,  NHV_ACCESS_RW)            \
    X(NHV_GUEST_DS_ACCESS,          NHV_WIDTH_32,  NHV_ACCESS_RW)            \
    X(NHV_GUEST_FS_ACCESS,          NHV_WIDTH_32,  NHV_ACCESS_RW)            \
    X(NHV_GUEST_GS_ACCESS,          NHV_WIDTH_32,  NHV_ACCESS_RW)            \
    X(NHV_GUEST_LDTR_ACCESS,        NHV_WIDTH_32,  NHV_ACCESS_RW)            \
    X(NHV_GUEST_TR_ACCESS,          NHV_WIDTH_32,  NHV_ACCESS_RW)            \
    X(NHV_GUEST_INTERRUPTIBILITY,   NHV_WIDTH_32,  NHV_ACCESS_RW)            \
    X(NHV_GUEST_ACTIVITY_STATE,     NHV_WIDTH_32,  NHV_ACCESS_RW)            \
    X(NHV_GUEST_SYSENTER_CS,        NHV_WIDTH_32,  NHV_ACCESS_RW)            \
    X(NHV_HOST_SYSENTER_CS,         NHV_WIDTH_32,  NHV_ACCESS_RW)            \
    X(NHV_EXIT_QUALIFICATION,       NHV_WIDTH_NAT, NHV_ACCESS_RO)            \
    X(NHV_IO_RCX,                   NHV_WIDTH_NAT, NHV_ACCESS_RO)            \
    X(NHV_IO_RSI,                   NHV_WIDTH_NAT, NHV_ACCESS_RO)            \
    X(NHV_IO_RDI,                   NHV_WIDTH_NAT, NHV_ACCESS_RO)            \
    X(NHV_IO_RIP,                   NHV_WIDTH_NAT, NHV_ACCESS_RO)            \
    X(NHV_GUEST_LINEAR_ADDRESS,     NHV_WIDTH_NAT, NHV_ACCESS_RO)            \
    X(NHV_GUEST_ES_BASE,            NHV_WIDTH_NAT, NHV_ACCESS_RW)            \
    X(NHV_GUEST_CS_BASE,            NHV_WIDTH_NAT, NHV_ACCESS_RW)            \
    X(NHV_GUEST_SS_BASE,            NHV_WIDTH_NAT, NHV_ACCESS_RW)            \
    X(NHV_GUEST_DS_BASE,            NHV_WIDTH_NAT, NHV_ACCESS_RW)            \
    X(NHV_GUEST_FS_BASE,            NHV_WIDTH_NAT, NHV_ACCESS_RW)            \
    X(NHV_GUEST_GS_BASE,            NHV_WIDTH_NAT, NHV_ACCESS_RW)            \
    X(NHV_GUEST_LDTR_BASE,          NHV_WIDTH_NAT, NHV_ACCESS_RW)            \
    X(NHV_GUEST_TR_BASE,            NHV_WIDTH_NAT, NHV_ACCESS_RW)            \
    X(NHV_GUEST_GDTR_BASE,          NHV_WIDTH_NAT, NHV_ACCESS_RW)            \
    X(NHV_GUEST_IDTR_BASE,          NHV_WIDTH_NAT, NHV_ACCESS_RW)            \
    X(NHV_GUEST_DR7,                NHV_WIDTH_NAT, NHV_ACCESS_RW)            \
    X(NHV_GUEST_RSP,                NHV_WIDTH_NAT, NHV_ACCESS_RW)            \
    X(NHV_GUEST_RIP,                NHV_WIDTH_NAT, NHV_ACCESS_RW)            \
    X(NHV_GUEST_RFLAGS,             NHV_WIDTH_NAT, NHV_ACCESS_RW)            \
    X(NHV_GUEST_PENDING_DBG_EXC,    NHV_WIDTH_NAT, NHV_ACCESS_RW)            \
    X(NHV_GUEST_SYSENTER_ESP,       NHV_WIDTH_NAT, NHV_ACCESS_RW)            \
    X(NHV_GUEST_SYSENTER_EIP,       NHV_WIDTH_NAT, NHV_ACCESS_RW)            \
    X(NHV_GUEST_CR0,                NHV_WIDTH_NAT, NHV_ACCESS_RW)            \
    X(NHV_GUEST_CR3,                NHV_WIDTH_NAT, NHV_ACCESS_RW)            \
    X(NHV_GUEST_CR4,                NHV_WIDTH_NAT, NHV_ACCESS_RW)            \
    X(NHV_HOST_CR0,                 NHV_WIDTH_NAT, NHV_ACCESS_RW)            \
    X(NHV_HOST_CR3,                 NHV_WIDTH_NAT, NHV_ACCESS_RW)            \
    X(NHV_HOST_CR4,                 NHV_WIDTH_NAT, NHV_ACCESS_RW)            \
    X(NHV_HOST_FS_BASE,             NHV_WIDTH_NAT, NHV_ACCESS_RW)            \
    X(NHV_HOST_GS_BASE,             NHV_WIDTH_NAT, NHV_ACCESS_RW)            \
    X(NHV_HOST_TR_BASE,             NHV_WIDTH_NAT, NHV_ACCESS_RW)            \
    X(NHV_HOST_GDTR_BASE,           NHV_WIDTH_NAT, NHV_ACCESS_RW)            \
    X(NHV_HOST_IDTR_BASE,           NHV_WIDTH_NAT, NHV_ACCESS_RW)            \
    X(NHV_HOST_SYSENTER_ESP,        NHV_WIDTH_NAT, NHV_ACCESS_RW)            \
    X(NHV_HOST_SYSENTER_EIP,        NHV_WIDTH_NAT, NHV_ACCESS_RW)            \
    X(NHV_HOST_RSP,                 NHV_WIDTH_NAT, NHV_ACCESS_RW)            \
    X(NHV_HOST_RIP,                 NHV_WIDTH_NAT, NHV_ACCESS_RW)

static const NHV_FIELD_DESC g_fields[] = {
#define NHV_DESC(enc, w, a) { (enc), (w), (a), 0 },
    NHV_FIELD_LIST(NHV_DESC)
#undef NHV_DESC
};

const NHV_FIELD_DESC* nhv_field_lookup(uint32_t encoding)
{
    uint32_t i;
    for (i = 0; i < (uint32_t)(sizeof(g_fields) / sizeof(g_fields[0])); ++i) {
        if (g_fields[i].encoding == encoding) {
            return &g_fields[i];
        }
    }
    return 0;
}

uint32_t nhv_field_count(void)
{
    return (uint32_t)(sizeof(g_fields) / sizeof(g_fields[0]));
}

static NHV_RESULT nhv_result(NHV_RESULT_KIND kind, uint32_t err)
{
    NHV_RESULT r;
    r.kind = kind;
    r.vm_instr_error = err;
    return r;
}

void nhv_vmcs12_store_init(NHV_VMCS12_STORE* store)
{
    if (store != 0) {
        memset(store, 0, sizeof(*store));
        store->current_index = NHV_VMCS12_NO_CURRENT_INDEX;
    }
}

static NHV_VMCS12_OBJECT* nhv_vmcs12_find_by_gpa(NHV_VMCS12_STORE* store, uint64_t gpa)
{
    uint32_t i;
    if (store == 0) {
        return 0;
    }
    for (i = 0; i < store->object_count; ++i) {
        if (store->objects[i].guest_phys_addr == gpa) {
            return &store->objects[i];
        }
    }
    return 0;
}

static NHV_VMCS12_OBJECT* nhv_vmcs12_current_mut(NHV_VMCS12_STORE* store)
{
    if (store == 0 ||
        store->current_index == NHV_VMCS12_NO_CURRENT_INDEX ||
        store->current_index < 0 ||
        (uint32_t)store->current_index >= store->object_count) {
        return 0;
    }
    return &store->objects[store->current_index];
}

const NHV_VMCS12_OBJECT* nhv_vmcs12_current(const NHV_VMCS12_STORE* store)
{
    if (store == 0 ||
        store->current_index == NHV_VMCS12_NO_CURRENT_INDEX ||
        store->current_index < 0 ||
        (uint32_t)store->current_index >= store->object_count) {
        return 0;
    }
    return &store->objects[store->current_index];
}

NHV_RESULT nhv_vmcs12_vmptrld(NHV_VMCS12_STORE* store, uint64_t gpa)
{
    NHV_VMCS12_OBJECT* obj;
    if (store == 0) {
        return nhv_result(NHV_VMFAIL_INVALID, 0);
    }

    obj = nhv_vmcs12_find_by_gpa(store, gpa);
    if (obj == 0) {
        if (store->object_count >= NHV_VMCS12_MAX_OBJECTS) {
            return nhv_result(NHV_VMFAIL_VALID, NHV_VMERR_UNSUPPORTED);
        }
        obj = &store->objects[store->object_count++];
        memset(obj, 0, sizeof(*obj));
        obj->guest_phys_addr = gpa;
        obj->launch_state = NHV_VMCS12_LAUNCH_CLEAR;
    }

    store->current_index = (int32_t)(obj - store->objects);
    return nhv_result(NHV_OK, 0);
}

NHV_RESULT nhv_vmcs12_vmclear(NHV_VMCS12_STORE* store, uint64_t gpa)
{
    NHV_VMCS12_OBJECT* obj;
    if (store == 0) {
        return nhv_result(NHV_VMFAIL_INVALID, 0);
    }

    obj = nhv_vmcs12_find_by_gpa(store, gpa);
    if (obj == 0) {
        if (store->object_count >= NHV_VMCS12_MAX_OBJECTS) {
            return nhv_result(NHV_VMFAIL_VALID, NHV_VMERR_UNSUPPORTED);
        }
        obj = &store->objects[store->object_count++];
        memset(obj, 0, sizeof(*obj));
        obj->guest_phys_addr = gpa;
    }

    obj->launch_state = NHV_VMCS12_LAUNCH_CLEAR;
    return nhv_result(NHV_OK, 0);
}

NHV_RESULT nhv_vmcs12_vmptrst(const NHV_VMCS12_STORE* store, uint64_t* gpa)
{
    const NHV_VMCS12_OBJECT* obj = nhv_vmcs12_current(store);
    if (obj == 0 || gpa == 0) {
        return nhv_result(NHV_VMFAIL_INVALID, 0);
    }
    *gpa = obj->guest_phys_addr;
    return nhv_result(NHV_OK, 0);
}

static NHV_VMCS_FIELD* nhv_vmcs12_find_field(NHV_VMCS12_OBJECT* obj, uint32_t encoding)
{
    uint32_t i;
    for (i = 0; i < obj->field_count; ++i) {
        if (obj->fields[i].encoding == encoding) {
            return &obj->fields[i];
        }
    }
    return 0;
}

NHV_RESULT nhv_vmcs12_vmread(const NHV_VMCS12_STORE* store, uint32_t enc, uint64_t* value)
{
    const NHV_FIELD_DESC* desc;
    const NHV_VMCS12_OBJECT* obj;
    uint32_t i;

    if (store == 0 || value == 0) {
        return nhv_result(NHV_VMFAIL_INVALID, 0);
    }
    desc = nhv_field_lookup(enc);
    if (desc == 0) {
        return nhv_result(NHV_VMFAIL_INVALID, 0);
    }
    obj = nhv_vmcs12_current(store);
    if (obj == 0) {
        return nhv_result(NHV_VMFAIL_INVALID, 0);
    }

    *value = 0;
    for (i = 0; i < obj->field_count; ++i) {
        if (obj->fields[i].encoding == enc && obj->fields[i].present) {
            *value = obj->fields[i].value;
            break;
        }
    }
    return nhv_result(NHV_OK, 0);
}

NHV_RESULT nhv_vmcs12_vmwrite(NHV_VMCS12_STORE* store, uint32_t enc, uint64_t value)
{
    const NHV_FIELD_DESC* desc;
    NHV_VMCS12_OBJECT* obj;
    NHV_VMCS_FIELD* slot;

    if (store == 0) {
        return nhv_result(NHV_VMFAIL_INVALID, 0);
    }
    desc = nhv_field_lookup(enc);
    if (desc == 0) {
        return nhv_result(NHV_VMFAIL_INVALID, 0);
    }
    if (desc->access == NHV_ACCESS_RO) {
        return nhv_result(NHV_VMFAIL_VALID, NHV_VMERR_WRITE_TO_READONLY);
    }
    obj = nhv_vmcs12_current_mut(store);
    if (obj == 0) {
        return nhv_result(NHV_VMFAIL_INVALID, 0);
    }

    slot = nhv_vmcs12_find_field(obj, enc);
    if (slot == 0) {
        if (obj->field_count >= NHV_VMCS12_MAX_FIELDS) {
            return nhv_result(NHV_VMFAIL_VALID, NHV_VMERR_UNSUPPORTED);
        }
        slot = &obj->fields[obj->field_count++];
        slot->encoding = enc;
    }

    slot->value = value;
    slot->present = 1;
    return nhv_result(NHV_OK, 0);
}

uint32_t nhv_vmcs12_launch_state(const NHV_VMCS12_STORE* store)
{
    const NHV_VMCS12_OBJECT* obj = nhv_vmcs12_current(store);
    return obj != 0 ? obj->launch_state : NHV_VMCS12_LAUNCH_CLEAR;
}

NHV_RESULT nhv_vmcs12_begin_launch(NHV_VMCS12_STORE* store)
{
    NHV_VMCS12_OBJECT* obj = nhv_vmcs12_current_mut(store);
    if (obj == 0) {
        return nhv_result(NHV_VMFAIL_INVALID, 0);
    }
    if (obj->launch_state != NHV_VMCS12_LAUNCH_CLEAR) {
        /* VMLAUNCH on a launched VMCS is a VMfailValid (error 5: VMLAUNCH with
         * non-clear VMCS). */
        return nhv_result(NHV_VMFAIL_VALID, 5u);
    }
    obj->launch_state = NHV_VMCS12_LAUNCH_LAUNCHED;
    return nhv_result(NHV_OK, 0);
}

NHV_RESULT nhv_vmcs12_begin_resume(NHV_VMCS12_STORE* store)
{
    NHV_VMCS12_OBJECT* obj = nhv_vmcs12_current_mut(store);
    if (obj == 0) {
        return nhv_result(NHV_VMFAIL_INVALID, 0);
    }
    if (obj->launch_state != NHV_VMCS12_LAUNCH_LAUNCHED) {
        /* VMRESUME on a clear VMCS is a VMfailValid (error 4: VMRESUME with
         * non-launched VMCS). */
        return nhv_result(NHV_VMFAIL_VALID, 4u);
    }
    return nhv_result(NHV_OK, 0);
}
