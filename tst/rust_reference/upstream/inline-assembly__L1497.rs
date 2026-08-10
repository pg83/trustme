// Extracted from src/inline-assembly.md:1497
#![allow(unused)]
fn main() {
    #[cfg(target_arch = "x86_64")] {
    #[unsafe(naked)]
    extern "sysv64-unwind" fn unwinding_naked() {
        core::arch::naked_asm!(
            // "CFI" here stands for "call frame information".
            ".cfi_startproc",
            // The CFA (canonical frame address) is the value of `rsp`
            // before the `call`, i.e. before the return address, `rip`,
            // was pushed to `rsp`, so it's eight bytes higher in memory
            // than `rsp` upon function entry (after `rip` has been
            // pushed).
            //
            // This is the default, so we don't have to write it.
            //".cfi_def_cfa rsp, 8",
            //
            // The traditional thing to do is to preserve the base
            // pointer, so we'll do that.
            "push rbp",
            // Since we've now extended the stack downward by 8 bytes in
            // memory, we need to adjust the offset to the CFA from `rsp`
            // by another 8 bytes.
            ".cfi_adjust_cfa_offset 8",
            // We also then annotate where we've stored the caller's value
            // of `rbp`, relative to the CFA, so that when unwinding into
            // the caller we can find it, in case we need it to calculate
            // the caller's CFA relative to it.
            //
            // Here, we've stored the caller's `rbp` starting 16 bytes
            // below the CFA.  I.e., starting from the CFA, there's first
            // the `rip` (which starts 8 bytes below the CFA and continues
            // up to it), then there's the caller's `rbp` that we just
            // pushed.
            ".cfi_offset rbp, -16",
            // As is traditional, we set the base pointer to the value of
            // the stack pointer.  This way, the base pointer stays the
            // same throughout the function body.
            "mov rbp, rsp",
            // We can now track the offset to the CFA from the base
            // pointer.  This means we don't need to make any further
            // adjustments until the end, as we don't change `rbp`.
            ".cfi_def_cfa_register rbp",
            // We can now call a function that may panic.
            "call {f}",
            // Upon return, we restore `rbp` in preparation for returning
            // ourselves.
            "pop rbp",
            // Now that we've restored `rbp`, we must specify the offset
            // to the CFA again in terms of `rsp`.
            ".cfi_def_cfa rsp, 8",
            // Now we can return.
            "ret",
            ".cfi_endproc",
            f = sym may_panic,
        )
    }
    
    extern "sysv64-unwind" fn may_panic() {
        panic!("unwind");
    }
    }
}
