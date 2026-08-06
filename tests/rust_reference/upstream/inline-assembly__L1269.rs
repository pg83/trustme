// Extracted from src/inline-assembly.md:1269
#![allow(unused)]
fn main() {
    #[cfg(target_arch = "x86_64")] {
    // nomem is strictly stronger than readonly, they can't be specified together
    unsafe { core::arch::asm!("", options(nomem, readonly)); }
    // ERROR: the `nomem` and `readonly` options are mutually exclusive
    }
    #[cfg(not(target_arch = "x86_64"))] core::compile_error!("Test not supported on this arch");
}
