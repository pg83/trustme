// Extracted from src/inline-assembly.md:223
#![allow(unused)]
fn main() {
    #[cfg(target_arch = "x86_64")] {
    // We also can't put explicit registers before positional operands
    unsafe { core::arch::asm!("/* {} */", in("eax") 0, in(reg) 5); }
    // ERROR: positional arguments cannot follow named arguments or explicit register arguments
    }
    #[cfg(not(target_arch = "x86_64"))] core::compile_error!("Test not supported on this arch");
}
