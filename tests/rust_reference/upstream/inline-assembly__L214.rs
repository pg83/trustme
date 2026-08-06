// Extracted from src/inline-assembly.md:214
#![allow(unused)]
fn main() {
    #[cfg(target_arch = "x86_64")] {
    // Named operands need to come after positional ones
    unsafe { core::arch::asm!("/* {x} {} */", x = const 5, in(reg) 5); }
    // ERROR: positional arguments cannot follow named arguments or explicit register arguments
    }
    #[cfg(not(target_arch = "x86_64"))] core::compile_error!("Test not supported on this arch");
}
