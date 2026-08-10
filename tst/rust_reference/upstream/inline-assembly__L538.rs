// Extracted from src/inline-assembly.md:538
#![allow(unused)]
fn main() {
    #[cfg(target_arch = "x86_64")] {
    // al overlaps with ax, so we can't name both of them.
    unsafe { core::arch::asm!("", in("ax") 5, in("al") 4i8); }
    // ERROR: register `al` conflicts with register `ax`
    }
    #[cfg(not(target_arch = "x86_64"))] core::compile_error!("Test not supported on this arch");
}
