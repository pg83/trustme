// Extracted from src/inline-assembly.md:1281
#![allow(unused)]
fn main() {
    #[cfg(target_arch = "x86_64")] {
    // pure blocks need at least one output
    unsafe { core::arch::asm!("", options(pure)); }
    // ERROR: asm with the `pure` option must have at least one output
    }
    #[cfg(not(target_arch = "x86_64"))] core::compile_error!("Test not supported on this arch");
}
