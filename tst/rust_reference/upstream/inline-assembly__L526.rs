// Extracted from src/inline-assembly.md:526
#![allow(unused)]
fn main() {
    #[cfg(target_arch = "x86_64")] {
    // ... even using different aliases
    unsafe { core::arch::asm!("", in("ax") 5, in("rax") 4); }
    // ERROR: register `rax` conflicts with register `ax`
    }
    #[cfg(not(target_arch = "x86_64"))] core::compile_error!("Test not supported on this arch");
}
