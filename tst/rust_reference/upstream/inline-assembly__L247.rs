// Extracted from src/inline-assembly.md:247
#![allow(unused)]
fn main() {
    #[cfg(target_arch = "x86_64")] {
    // We have to name all of the operands in the format string
    unsafe { core::arch::asm!("", in(reg) 5, x = const 5); }
    // ERROR: multiple unused asm arguments
    }
    #[cfg(not(target_arch = "x86_64"))] core::compile_error!("Test not supported on this arch");
}
