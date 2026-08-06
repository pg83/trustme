// Extracted from src/inline-assembly.md:900
#![allow(unused)]
fn main() {
    #[cfg(target_arch = "x86_64")] {
    // We can't specify both `r` and `e` at the same time.
    unsafe { core::arch::asm!("/* {:er}", in(reg) 5i32); }
    // ERROR: asm template modifier must be a single character
    }
    #[cfg(not(target_arch = "x86_64"))] core::compile_error!("Test not supported on this arch");
}
