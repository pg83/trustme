// Extracted from src/inline-assembly.md:518
#![allow(unused)]
fn main() {
    #[cfg(target_arch = "x86_64")] {
    // We can't name eax twice
    unsafe { core::arch::asm!("", in("eax") 5, in("eax") 4); }
    // ERROR: register `eax` conflicts with register `eax`
    }
    #[cfg(not(target_arch = "x86_64"))] core::compile_error!("Test not supported on this arch");
}
