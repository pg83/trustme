// Extracted from src/inline-assembly.md:235
#![allow(unused)]
fn main() {
    #[cfg(target_arch = "x86_64")] {
    // Explicit register operands don't get substituted, use `eax` explicitly in the string
    unsafe { core::arch::asm!("/* {} */", in("eax") 5); }
    // ERROR: invalid reference to argument at index 0
    }
    #[cfg(not(target_arch = "x86_64"))] core::compile_error!("Test not supported on this arch");
}
