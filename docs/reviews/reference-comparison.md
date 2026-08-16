# Reference comparison: NestedHv vs kHypervisor vs teacher (IDA)

Compared the NestedHv exit/reflection path against two references:

- Open source: `kHypervisor/kHypervisor/vmx.cpp` (KelvinChan, MIT). Functions
  `SaveExceptionInformationFromVmcs02`, `SaveGuestFieldFromVmcs02`,
  `LoadHostStateForLevel1`, `VmEntryCheck`.
- Teacher (IDA): `ReverseLabV3/hv_nested_capability_check_20260810/output/vmx_exit_tables.json`
  (the L1's exit table and VMX-instruction handlers).

## What the comparison found (honest list)

### Fixed in this pass

1. Reflection was writing only 5 fields. kHypervisor writes the full set in two
   phases. `nhv_exit_reflect` now writes the complete snapshot:
   - Exit info: reason, qualification, exit interruption info/error, exit
     instruction length, IDT vectoring info/error, VM-instruction error,
     VMX-instruction info, guest linear/physical address (11 fields).
   - Guest state: RIP/RSP/RFLAGS/CR0/CR3/CR4/DR7/EFER, all 8 segment
     selectors, all segment limits, all AR bytes, GDTR/IDTR limits, all segment
     bases, GDTR/IDTR bases, interruptibility, activity state, SYSENTER CS/ESP/
     EIP, pending debug exceptions (~40 fields).

### Known gaps (not yet done; by design or backlog)

1. No VM-entry guest-state validation. kHypervisor `VmEntryCheck` validates CR0/
   CR4 fixed bits, segment AR/type/DPL, RFLAGS reserved bits, descriptor-table
   limits, canonicality, activity state before VMLAUNCH. NestedHv's
   `nhv_vmcs02_build` merges controls and copies guest state but does not check
   it; a real VMLAUNCH with bad guest state would surface as exit reason 33
   (VM-entry failure), not error 7.
2. VMCS field model is partial: only full-width encodings, ~100 fields. The
   low/high 32-bit half encodings of 64-bit fields are not modeled. The
   teacher's VMREAD handler (`sub_14012A670`) decodes the full encoding
   (bits 28/23/18/14, width/type/index).
3. No VMCS01 concept yet. kHypervisor keeps three regions: vmcs01 (real VMCS
   running L1), vmcs02 (real VMCS running L2), vmcs12 (L1's shadow). NestedHv
   models vmcs12 (shadow) + a vmcs02 plan; the vmcs01 that actually runs L1 is
   a later step (kernel driver).
4. VMXON/VMXOFF/VMCLEAR/VMPTRLD emulation in NestedHv is semantic-only
   (no revision-ID check, page-alignment, canonicality, instruction-length
   advance). kHypervisor's `VmxVmxonEmulate` etc. do full checks.

### Places NestedHv already matches the references

1. Exit classification (reflect iff L1 requested the control, else L0 handles;
   VMX instructions always L0-owned) matches the SDM reflect model and
   kHypervisor's root/guest-mode rule. CPUID/INVD/XSETBV treated as
   unconditional L0-handled, consistent with the SDM (the teacher additionally
   reflects CPUID to hide its hypervisor leaf - a teacher-specific choice).
2. Field encoding scheme (width<<14 | high<<13 | type<<10 | index) matches the
   teacher's VMREAD decode.
3. Exit-reason numbering for VMX instructions (18..26) matches the SDM and the
   teacher's table.

## Verdict

The core skeleton is directionally correct, but the reflection path was
incomplete relative to both references; that is now fixed. The remaining gaps
are backlog items, the largest being VM-entry guest-state validation.
