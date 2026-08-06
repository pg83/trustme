// Extracted from src/inline-assembly.md:202
#![allow(unused)]
fn main() {
    let x = 5;
    #[cfg(target_arch = "x86_64")] {
    // The template strings need to appear first in the asm invocation
    unsafe { core::arch::asm!("/* {x} */", x = const 5, "ud2"); } // ERROR: unexpected token
    }
    #[cfg(not(target_arch = "x86_64"))] core::compile_error!("Test not supported on this arch");
}
