# NestedHv

A clean, minimal Intel VT-x **nested virtualization** hypervisor (L0 -> L1 -> L2),
written from scratch.

## What this is

- A generic L0 hypervisor that virtualizes another hypervisor (L1), which in turn
  runs its own guest (L2).
- Implements the nested-VMX core: VMXON/VMPTRLD/VMREAD/VMWRITE/VMLAUNCH/VMRESUME/
  VMXOFF emulation, a VMCS12 shadow, VMCS02 build/merge, and exit routing.
- Hardware-free host tests for every piece of pure logic, so correctness is
  verifiable without a test machine.

## What this is NOT

- It does not target any specific third-party product, hypervisor, or protection.
- It does not observe logins, accounts, sessions, or credentials.
- It does not preempt or attach to any existing L0/L1 on a machine.

## Layout

```
include/     public headers (field encodings, VMCS12 store, results)
src/         implementation (pure C, hardware-free where possible)
tests/host/  host tests that compile and run on a normal machine
```

## Status

- [x] VMCS field encoding scheme + field table
- [x] VMCS12 shadow object (VMPTRLD/VMREAD/VMWRITE/VMCLEAR semantics)
- [x] VMCS02 build/merge (control merge + validation, guest-state copy, L0 host state)
- [x] exit classification and reflection (reflect vs L0-owned vs unsupported)
- [ ] VMX instruction wrappers (asm)
- [ ] minimal L1 -> L2 demo

## License

MIT
